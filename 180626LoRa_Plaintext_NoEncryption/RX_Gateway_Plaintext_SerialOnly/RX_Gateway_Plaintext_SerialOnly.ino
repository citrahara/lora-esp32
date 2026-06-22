#include <Arduino.h>
#include <SPI.h>
#include <LoRa.h>
#include <cstring>
#include <time.h>
#include <sys/time.h>

// =============================================================
// RX SERIAL ONLY: LoRa plaintext packet -> Serial table
// Tidak pakai Xoodyak, tidak decrypt, tidak ada WiFi/hotspot/WebServer.
// Dashboard web tetap dijalankan di laptop penerima lewat USB Serial.
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

// Samakan frekuensi TX dan RX. Kalau modul Anda pakai 439 MHz, ubah dua-duanya.
#define LORA_FREQ  435E6

// -----------------------
// LED indikator paket masuk
// -----------------------
#ifndef LED_BUILTIN
#define LED_BUILTIN 48
#endif
#define RX_LED_PIN LED_BUILTIN

#define EXPECTED_SEND_INTERVAL_SECONDS 4

struct SensorRecord {
  bool valid = false;
  String status = "Belum ada paket";
  String node = "-";
  uint32_t counter = 0;
  String timestamp = "-";
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
time_t lastPrintedEpochWIB = 0;

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

  struct timeval tv;
  tv.tv_sec = mktime(&tmCompile);
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

bool parseTimestampWIB(const String &timestampText, time_t &epochOut) {
  int year = 0, month = 0, day = 0;
  int hour = 0, minute = 0, second = 0;

  if (sscanf(timestampText.c_str(), "%d-%d-%d %d:%d:%d", &year, &month, &day, &hour, &minute, &second) != 6) {
    return false;
  }

  if (year < 2020 || month < 1 || month > 12 || day < 1 || day > 31 ||
      hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 59) {
    return false;
  }

  struct tm tmValue;
  memset(&tmValue, 0, sizeof(tmValue));
  tmValue.tm_year = year - 1900;
  tmValue.tm_mon = month - 1;
  tmValue.tm_mday = day;
  tmValue.tm_hour = hour;
  tmValue.tm_min = minute;
  tmValue.tm_sec = second;
  tmValue.tm_isdst = 0;

  epochOut = mktime(&tmValue);
  return epochOut > 0;
}

String formatTimestampWIB(time_t epoch) {
  struct tm timeinfo;
  char buffer[24];

  if (!localtime_r(&epoch, &timeinfo)) {
    strncpy(buffer, "NO_TIME", sizeof(buffer));
    buffer[sizeof(buffer) - 1] = '\0';
    return String(buffer);
  }

  strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeinfo);
  return String(buffer);
}

String monotonicTimestampWIB(const String &candidateTimestamp) {
  time_t candidateEpoch = 0;

  if (!parseTimestampWIB(candidateTimestamp, candidateEpoch)) {
    time(&candidateEpoch);
  }

  if (lastPrintedEpochWIB > 0 && candidateEpoch <= lastPrintedEpochWIB) {
    candidateEpoch = lastPrintedEpochWIB + EXPECTED_SEND_INTERVAL_SECONDS;
  }

  lastPrintedEpochWIB = candidateEpoch;
  return formatTimestampWIB(candidateEpoch);
}

bool setClockFromTimestampWIB(const String &timestampText) {
  time_t epoch = 0;
  if (!parseTimestampWIB(timestampText, epoch)) {
    return false;
  }

  struct timeval tv;
  tv.tv_sec = epoch;
  tv.tv_usec = 0;
  settimeofday(&tv, nullptr);
  return true;
}

void handleSerialTimeSync() {
  static String input = "";

  while (Serial.available()) {
    const char c = (char)Serial.read();

    if (c == '\n' || c == '\r') {
      input.trim();
      if (input.startsWith("TIME_SYNC=")) {
        String stamp = input.substring(strlen("TIME_SYNC="));
        stamp.trim();
        if (setClockFromTimestampWIB(stamp)) {
          Serial.print("TIME_SYNC_OK | ");
          Serial.println(stamp);
        } else {
          Serial.print("TIME_SYNC_INVALID | ");
          Serial.println(stamp);
        }
      }
      input = "";
      continue;
    }

    if (input.length() < 48) {
      input += c;
    }
  }
}

String getField(const String &src, const String &key) {
  int pos = 0;

  while (pos < src.length()) {
    int sep = src.indexOf(';', pos);
    if (sep < 0) sep = src.length();

    String token = src.substring(pos, sep);
    token.trim();

    const int eq = token.indexOf('=');
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

void markFailure(const String &status, const String &packet, int rssi, float snr) {
  failedPackets++;

  char nowWIB[24];
  getTimestampWIB(nowWIB, sizeof(nowWIB));

  latest.valid = false;
  latest.status = status;
  latest.timestamp = monotonicTimestampWIB(String(nowWIB));
  latest.rxMs = millis();
  latest.rssi = rssi;
  latest.snr = snr;
  latest.raw = packet;
  latest.plaintext = "-";

  printRxFailRow(status, latest.timestamp, rssi, snr);
  Serial.print("RAW_RX_INVALID | ");
  Serial.println(packet);
}

bool hasRequiredFields(const String &packet) {
  return getField(packet, "ver").length() > 0 &&
         getField(packet, "node").length() > 0 &&
         getField(packet, "ctr").length() > 0 &&
         getField(packet, "ts").length() > 0 &&
         getField(packet, "v").length() > 0 &&
         getField(packet, "i").length() > 0 &&
         getField(packet, "p").length() > 0;
}

void processPacket(const String &packet, int rssi, float snr) {
  totalPackets++;

  if (!hasRequiredFields(packet)) {
    markFailure("FORMAT_INVALID", packet, rssi, snr);
    return;
  }

  SensorRecord rec;
  rec.valid = true;
  rec.status = "Paket plaintext valid";
  rec.node = getField(packet, "node");
  rec.counter = getField(packet, "ctr").toInt();

  String txTimestamp = getField(packet, "rt");
  if (txTimestamp.length() == 0) {
    char nowWIB[24];
    getTimestampWIB(nowWIB, sizeof(nowWIB));
    txTimestamp = String(nowWIB);
  }

  rec.timestamp = monotonicTimestampWIB(txTimestamp);
  rec.txMs = getField(packet, "ts").toInt();
  rec.rxMs = millis();
  rec.voltage = getField(packet, "v").toFloat();
  rec.current = getField(packet, "i").toFloat();
  rec.power = getField(packet, "p").toFloat();
  rec.rssi = rssi;
  rec.snr = snr;
  rec.plaintext = packet;
  rec.raw = packet;

  latest = rec;
  validPackets++;
  blinkRxLed();

  printRxValidRow(rec);
  Serial.print("PLAIN_RX | ");
  Serial.println(packet);
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(RX_LED_PIN, OUTPUT);
  digitalWrite(RX_LED_PIN, LOW);

  initClockFromCompileTimeWIB();

  Serial.println();
  Serial.println("=== RX LoRa Plaintext SERIAL ONLY ===");
  Serial.println("Tidak pakai WiFi, tidak pakai hotspot, tidak ada WebServer di ESP32.");
  Serial.println("Payload diterima langsung tanpa dekripsi. Gunakan hanya untuk pengujian atau jaringan yang tidak membutuhkan kerahasiaan.");

  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_SS);
  LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);

  if (!LoRa.begin(LORA_FREQ)) {
    Serial.println("LoRa init gagal. Cek wiring, power 3.3V, antena, dan frekuensi.");
    while (true) delay(1000);
  }

  LoRa.setSpreadingFactor(7);
  LoRa.setSignalBandwidth(125E3);
  LoRa.setCodingRate4(5);
  LoRa.enableCrc();

  Serial.println("LoRa RX plaintext siap. Menunggu paket LoRa...");
  printRxHeader();
}

void loop() {
  handleSerialTimeSync();

  const int packetSize = LoRa.parsePacket();
  if (!packetSize) {
    return;
  }

  String packet;
  packet.reserve(packetSize + 8);

  while (LoRa.available()) {
    packet += (char)LoRa.read();
  }

  packet.trim();

  const int rssi = LoRa.packetRssi();
  const float snr = LoRa.packetSnr();
  processPacket(packet, rssi, snr);
}
