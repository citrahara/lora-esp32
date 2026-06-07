#include <Arduino.h>
#include <SPI.h>
#include <LoRa.h>
#include <cstring>
#include <time.h>
#include <sys/time.h>

#include "xoodyak.hpp"

// =============================================================
// RX SERIAL ONLY: LoRa packet -> Xoodyak verified decrypt -> Serial table
// Tidak pakai WiFi, tidak bikin hotspot, tidak ada WebServer di ESP32.
// Dashboard web dijalankan di laptop lewat USB Serial: http://127.0.0.1:8000
// =============================================================

// -----------------------
// Pin LoRa ESP32-S3
// -----------------------
#define LORA_SCK   12
#define LORA_MISO  13
#define LORA_MOSI  11
#define LORA_SS    10
#define LORA_RST   5
#define LORA_DIO0  4
#define LORA_FREQ  433E6

// -----------------------
// LED indikator paket masuk
// -----------------------
#ifndef LED_BUILTIN
#define LED_BUILTIN 48
#endif
#define RX_LED_PIN LED_BUILTIN

// -----------------------
// Xoodyak config
// -----------------------
#define KEY_LEN   16
#define NONCE_LEN 16
#define TAG_LEN   16
#define MAX_TEXT_LEN 128

uint8_t XOODYAK_KEY[KEY_LEN] = {
  0x10, 0x21, 0x32, 0x43,
  0x54, 0x65, 0x76, 0x87,
  0x98, 0xA9, 0xBA, 0xCB,
  0xDC, 0xED, 0xFE, 0x0F
};

struct SensorRecord {
  bool valid = false;
  String status = "Belum ada paket";
  String node = "-";
  uint32_t counter = 0;
  String timestamp = "-";      // rt dari TX: yyyy-mm-dd hh:mm:ss GMT+7/WIB
  unsigned long txMs = 0;
  unsigned long rxMs = 0;
  float voltage = 0.0;
  float current = 0.0;
  float power = 0.0;
  int rssi = 0;
  float snr = 0.0;
  String plaintext = "-";
  String raw = "-";
};

SensorRecord latest;
uint32_t totalPackets = 0;
uint32_t validPackets = 0;
uint32_t failedPackets = 0;
bool rxHeaderPrinted = false;

int monthFromText(const char *mon) {
  if (strcmp(mon, "Jan") == 0) return 0;
  if (strcmp(mon, "Feb") == 0) return 1;
  if (strcmp(mon, "Mar") == 0) return 2;
  if (strcmp(mon, "Apr") == 0) return 3;
  if (strcmp(mon, "May") == 0) return 4;
  if (strcmp(mon, "Jun") == 0) return 5;
  if (strcmp(mon, "Jul") == 0) return 6;
  if (strcmp(mon, "Aug") == 0) return 7;
  if (strcmp(mon, "Sep") == 0) return 8;
  if (strcmp(mon, "Oct") == 0) return 9;
  if (strcmp(mon, "Nov") == 0) return 10;
  if (strcmp(mon, "Dec") == 0) return 11;
  return 0;
}

void initClockFromCompileTimeWIB() {
  // Dipakai hanya untuk timestamp lokal jika paket gagal sebelum plaintext terbaca.
  setenv("TZ", "WIB-7", 1);
  tzset();

  char monText[4] = {0};
  int day = 1, year = 2026;
  int hour = 0, minute = 0, second = 0;

  sscanf(__DATE__, "%3s %d %d", monText, &day, &year);
  sscanf(__TIME__, "%d:%d:%d", &hour, &minute, &second);

  struct tm tmCompile;
  memset(&tmCompile, 0, sizeof(tmCompile));
  tmCompile.tm_year = year - 1900;
  tmCompile.tm_mon = monthFromText(monText);
  tmCompile.tm_mday = day;
  tmCompile.tm_hour = hour;
  tmCompile.tm_min = minute;
  tmCompile.tm_sec = second;
  tmCompile.tm_isdst = 0;

  time_t epoch = mktime(&tmCompile);
  struct timeval tv;
  tv.tv_sec = epoch;
  tv.tv_usec = 0;
  settimeofday(&tv, nullptr);
}

bool getTimestampWIB(char *buffer, size_t len) {
  time_t now;
  time(&now);

  struct tm timeinfo;
  if (!localtime_r(&now, &timeinfo)) {
    strncpy(buffer, "NO_TIME", len);
    buffer[len - 1] = '\0';
    return false;
  }

  strftime(buffer, len, "%Y-%m-%d %H:%M:%S", &timeinfo);
  return true;
}

void printRxHeader() {
  Serial.println();
  Serial.println("====================================================================================================================================");
  Serial.println("DIR | NODE |   CTR | TIME GMT+7         |      TXms |      RXms |    V(V) |    I(mA) |    P(mW) | RSSI |    SNR | STATUS");
  Serial.println("------------------------------------------------------------------------------------------------------------------------------------");
  rxHeaderPrinted = true;
}

void printRxValidRow(const SensorRecord &rec) {
  if (!rxHeaderPrinted) {
    printRxHeader();
  }

  char line[230];
  snprintf(
    line,
    sizeof(line),
    "%-3s | %-4s | %5lu | %-19s | %9lu | %9lu | %7.3f | %8.2f | %8.2f | %4d | %6.2f | %s",
    "RX",
    rec.node.c_str(),
    (unsigned long)rec.counter,
    rec.timestamp.c_str(),
    rec.txMs,
    rec.rxMs,
    rec.voltage,
    rec.current,
    rec.power,
    rec.rssi,
    rec.snr,
    "VALID"
  );
  Serial.println(line);
}

void printRxFailRow(const String &status, const String &timeWIB, int rssi, float snr) {
  if (!rxHeaderPrinted) {
    printRxHeader();
  }

  char line[230];
  snprintf(
    line,
    sizeof(line),
    "%-3s | %-4s | %5s | %-19s | %9s | %9lu | %7s | %8s | %8s | %4d | %6.2f | %s",
    "RX",
    "-",
    "-",
    timeWIB.c_str(),
    "-",
    millis(),
    "-",
    "-",
    "-",
    rssi,
    snr,
    status.c_str()
  );
  Serial.println(line);
}

int hexValue(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

bool hexToBytes(const String &hex, uint8_t *out, size_t maxOutLen, size_t &outLen) {
  if (hex.length() % 2 != 0) return false;

  size_t byteLen = hex.length() / 2;
  if (byteLen > maxOutLen) return false;

  for (size_t i = 0; i < byteLen; i++) {
    int high = hexValue(hex[i * 2]);
    int low = hexValue(hex[i * 2 + 1]);
    if (high < 0 || low < 0) return false;
    out[i] = (uint8_t)((high << 4) | low);
  }

  outLen = byteLen;
  return true;
}

String getField(const String &src, const String &key) {
  int pos = 0;

  while (pos < src.length()) {
    int sep = src.indexOf(';', pos);
    if (sep < 0) sep = src.length();

    String token = src.substring(pos, sep);
    token.trim();

    int eq = token.indexOf('=');
    if (eq > 0) {
      String tokenKey = token.substring(0, eq);
      String tokenValue = token.substring(eq + 1);
      tokenKey.trim();
      tokenValue.trim();

      if (tokenKey == key) return tokenValue;
    }

    pos = sep + 1;
  }

  return "";
}

void blinkRxLed() {
  digitalWrite(RX_LED_PIN, HIGH);
  delay(35);
  digitalWrite(RX_LED_PIN, LOW);
}

bool decryptPayload(
  const String &associatedData,
  const String &cipherHex,
  const String &tagHex,
  const uint8_t nonce[NONCE_LEN],
  String &plaintext
) {
  uint8_t cipher[MAX_TEXT_LEN];
  uint8_t tag[TAG_LEN];
  uint8_t plain[MAX_TEXT_LEN + 1];

  memset(cipher, 0, sizeof(cipher));
  memset(tag, 0, sizeof(tag));
  memset(plain, 0, sizeof(plain));

  size_t cipherLen = 0;
  size_t tagLen = 0;

  bool cipherOk = hexToBytes(cipherHex, cipher, MAX_TEXT_LEN, cipherLen);
  bool tagOk = hexToBytes(tagHex, tag, TAG_LEN, tagLen);

  if (!cipherOk || !tagOk || tagLen != TAG_LEN) return false;

  bool verified = xoodyak::decrypt(
    XOODYAK_KEY,
    nonce,
    tag,
    (const uint8_t *)associatedData.c_str(),
    associatedData.length(),
    cipher,
    plain,
    cipherLen
  );

  if (!verified) {
    plaintext = "";
    return false;
  }

  plain[cipherLen] = '\0';
  plaintext = String((char *)plain);
  return true;
}

void markFailure(const String &status, const String &packet, int rssi, float snr) {
  failedPackets++;

  char nowWIB[24];
  getTimestampWIB(nowWIB, sizeof(nowWIB));

  latest.valid = false;
  latest.status = status;
  latest.timestamp = String(nowWIB);
  latest.rxMs = millis();
  latest.rssi = rssi;
  latest.snr = snr;
  latest.raw = packet;
  latest.plaintext = "-";

  printRxFailRow(status, latest.timestamp, rssi, snr);
}

void processPacket(const String &packet, int rssi, float snr) {
  totalPackets++;

  String ver = getField(packet, "ver");
  String node = getField(packet, "node");
  String ctr = getField(packet, "ctr");
  String nonceHex = getField(packet, "nonce");
  String cipherHex = getField(packet, "ct");
  String tagHex = getField(packet, "tag");

  if (ver.length() == 0 || node.length() == 0 || ctr.length() == 0 ||
      nonceHex.length() == 0 || cipherHex.length() == 0 || tagHex.length() == 0) {
    markFailure("FORMAT_INVALID", packet, rssi, snr);
    return;
  }

  uint8_t nonce[NONCE_LEN];
  size_t nonceLen = 0;
  bool nonceOk = hexToBytes(nonceHex, nonce, NONCE_LEN, nonceLen);

  if (!nonceOk || nonceLen != NONCE_LEN) {
    markFailure("NONCE_INVALID", packet, rssi, snr);
    return;
  }

  String associatedData = "";
  associatedData.reserve(40);
  associatedData += "ver=";
  associatedData += ver;
  associatedData += ";";
  associatedData += "node=";
  associatedData += node;
  associatedData += ";";
  associatedData += "ctr=";
  associatedData += ctr;

  String plaintext = "";
  bool ok = decryptPayload(associatedData, cipherHex, tagHex, nonce, plaintext);

  if (!ok) {
    markFailure("DEKRIPSI_GAGAL", packet, rssi, snr);
    return;
  }

  SensorRecord rec;
  rec.valid = true;
  rec.status = "Paket valid";

  String devFromPlaintext = getField(plaintext, "dev");
  rec.node = devFromPlaintext.length() > 0 ? devFromPlaintext : node;

  rec.counter = ctr.toInt();
  rec.timestamp = getField(plaintext, "rt");
  if (rec.timestamp.length() == 0) {
    char nowWIB[24];
    getTimestampWIB(nowWIB, sizeof(nowWIB));
    rec.timestamp = String(nowWIB);
  }

  rec.txMs = getField(plaintext, "ts").toInt();
  rec.rxMs = millis();
  rec.voltage = getField(plaintext, "v").toFloat();
  rec.current = getField(plaintext, "i").toFloat();
  rec.power = getField(plaintext, "p").toFloat();
  rec.rssi = rssi;
  rec.snr = snr;
  rec.plaintext = plaintext;
  rec.raw = packet;

  latest = rec;
  validPackets++;
  blinkRxLed();

  printRxValidRow(rec);
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(RX_LED_PIN, OUTPUT);
  digitalWrite(RX_LED_PIN, LOW);

  initClockFromCompileTimeWIB();

  Serial.println();
  Serial.println("=== RX LoRa + Xoodyak SERIAL ONLY ===");
  Serial.println("Tidak pakai WiFi, tidak pakai hotspot, tidak ada WebServer di ESP32.");
  Serial.println("Dashboard web dijalankan di laptop dari data USB Serial.");
  Serial.println("Timestamp valid diambil dari TX field rt=... setelah decrypt.");

  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_SS);
  LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);

  if (!LoRa.begin(LORA_FREQ)) {
    Serial.println("LoRa init gagal. Cek wiring, power 3.3V, antena, frekuensi.");
    while (true) {
      delay(1000);
    }
  }

  LoRa.setSpreadingFactor(7);
  LoRa.setSignalBandwidth(125E3);
  LoRa.setCodingRate4(5);

  Serial.println("LoRa RX siap. Menunggu paket LoRa...");
  printRxHeader();
}

void loop() {
  int packetSize = LoRa.parsePacket();
  if (packetSize) {
    String packet = "";
    packet.reserve(packetSize + 8);

    while (LoRa.available()) {
      packet += (char)LoRa.read();
    }

    int rssi = LoRa.packetRssi();
    float snr = LoRa.packetSnr();
    processPacket(packet, rssi, snr);
  }
}
