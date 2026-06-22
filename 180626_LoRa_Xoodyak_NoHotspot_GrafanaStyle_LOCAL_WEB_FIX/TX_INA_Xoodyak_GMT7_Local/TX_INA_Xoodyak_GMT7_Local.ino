#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <LoRa.h>
#include <Adafruit_INA219.h>
#include <cstring>
#include <time.h>
#include <sys/time.h>

#include "xoodyak.hpp"

// =============================================================
// TX: INA219 -> plaintext sensor -> Xoodyak AEAD -> LoRa packet
// Serial Monitor TX menampilkan:
// 1. Baris TX sensor
// 2. Baris ENC_TX: AD, NONCE, CT, TAG
// 3. Baris PLAIN_TX
// 4. Baris RAW_TX
//
// Dashboard web tetap membaca COM RX.
// Jadi agar dashboard web menampilkan enkripsi, RX juga harus mencetak:
// ENC | NODE=K1 | CTR=... | AD=... | NONCE=... | CT=... | TAG=...
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
#define LORA_FREQ  435E6

// -----------------------
// Pin I2C INA219
// -----------------------
#define I2C_SDA 8
#define I2C_SCL 9

// -----------------------
// LED indikator pengiriman
// -----------------------
#ifndef LED_BUILTIN
#define LED_BUILTIN 48
#endif
#define TX_LED_PIN LED_BUILTIN

// -----------------------
// Konfigurasi aplikasi
// -----------------------
#define NODE_ID "K1"
#define SEND_INTERVAL_MS 4000UL

#define KEY_LEN   16
#define NONCE_LEN 16
#define TAG_LEN   16
#define MAX_TEXT_LEN 128

Adafruit_INA219 ina219;

uint32_t packetCounter = 0;
unsigned long nextSendMs = 0;
bool txHeaderPrinted = false;

// Key demo 128-bit.
// WAJIB sama dengan key di RX.
uint8_t XOODYAK_KEY[KEY_LEN] = {
  0x10, 0x21, 0x32, 0x43,
  0x54, 0x65, 0x76, 0x87,
  0x98, 0xA9, 0xBA, 0xCB,
  0xDC, 0xED, 0xFE, 0x0F
};

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
  // WIB-7 berarti UTC+7 dalam format TZ POSIX.
  // Ya, tandanya memang terasa kebalik. Standar waktu juga ikut bercanda.
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

void printTxHeader() {
  Serial.println();
  Serial.println("================================================================================================================");
  Serial.println("DIR | NODE |   CTR | TIME GMT+7         |      TXms |    V(V) |    I(mA) |    P(mW) | STATUS");
  Serial.println("----------------------------------------------------------------------------------------------------------------");
  txHeaderPrinted = true;
}

void printTxRow(
  uint32_t ctr,
  const char *timeWIB,
  unsigned long txMs,
  float voltageV,
  float currentmA,
  float powermW
) {
  if (!txHeaderPrinted) {
    printTxHeader();
  }

  char line[190];

  snprintf(
    line,
    sizeof(line),
    "%-3s | %-4s | %5lu | %-19s | %9lu | %7.3f | %8.2f | %8.2f | %s",
    "TX",
    NODE_ID,
    (unsigned long)ctr,
    timeWIB,
    txMs,
    voltageV,
    currentmA,
    powermW,
    "TERKIRIM"
  );

  Serial.println(line);
}

void printTxEncryptionRow(
  uint32_t ctr,
  const String &associatedData,
  const String &nonceHex,
  const String &cipherHex,
  const String &tagHex,
  const String &plaintext,
  const String &packet
) {
  Serial.print("ENC_TX | NODE=");
  Serial.print(NODE_ID);

  Serial.print(" | CTR=");
  Serial.print((unsigned long)ctr);

  Serial.print(" | AD=");
  Serial.print(associatedData);

  Serial.print(" | NONCE=");
  Serial.print(nonceHex);

  Serial.print(" | CT=");
  Serial.print(cipherHex);

  Serial.print(" | TAG=");
  Serial.println(tagHex);

  Serial.print("PLAIN_TX | ");
  Serial.println(plaintext);

  Serial.print("RAW_TX | ");
  Serial.println(packet);
}

String bytesToHex(const uint8_t *data, size_t len) {
  const char hexChars[] = "0123456789ABCDEF";
  String result = "";
  result.reserve(len * 2);

  for (size_t i = 0; i < len; i++) {
    result += hexChars[(data[i] >> 4) & 0x0F];
    result += hexChars[data[i] & 0x0F];
  }

  return result;
}

void blinkTxLed() {
  digitalWrite(TX_LED_PIN, HIGH);
  delay(40);
  digitalWrite(TX_LED_PIN, LOW);
}

void makeNonce(uint8_t nonce[NONCE_LEN], uint32_t counter) {
  memset(nonce, 0, NONCE_LEN);

  nonce[0] = (counter >> 24) & 0xFF;
  nonce[1] = (counter >> 16) & 0xFF;
  nonce[2] = (counter >> 8) & 0xFF;
  nonce[3] = counter & 0xFF;

  nonce[4] = NODE_ID[0];
  nonce[5] = NODE_ID[1];

  uint32_t r1 = esp_random();
  uint32_t r2 = esp_random();
  uint16_t r3 = esp_random() & 0xFFFF;

  nonce[6]  = (r1 >> 24) & 0xFF;
  nonce[7]  = (r1 >> 16) & 0xFF;
  nonce[8]  = (r1 >> 8) & 0xFF;
  nonce[9]  = r1 & 0xFF;

  nonce[10] = (r2 >> 24) & 0xFF;
  nonce[11] = (r2 >> 16) & 0xFF;
  nonce[12] = (r2 >> 8) & 0xFF;
  nonce[13] = r2 & 0xFF;

  nonce[14] = (r3 >> 8) & 0xFF;
  nonce[15] = r3 & 0xFF;
}

String buildPlaintext(
  float voltageV,
  float currentmA,
  float powermW,
  unsigned long sentMs,
  const char *timeWIB
) {
  String payload = "";
  payload.reserve(125);

  payload += "dev=";
  payload += String(NODE_ID);
  payload += ";";

  payload += "rt=";
  payload += String(timeWIB);
  payload += ";";

  payload += "ts=";
  payload += String(sentMs);
  payload += ";";

  payload += "v=";
  payload += String(voltageV, 3);
  payload += ";";

  payload += "i=";
  payload += String(currentmA, 2);
  payload += ";";

  payload += "p=";
  payload += String(powermW, 2);

  return payload;
}

bool encryptPayload(
  const String &associatedData,
  const String &plaintext,
  const uint8_t nonce[NONCE_LEN],
  String &cipherHex,
  String &tagHex
) {
  if (plaintext.length() > MAX_TEXT_LEN) {
    return false;
  }

  uint8_t cipher[MAX_TEXT_LEN];
  uint8_t tag[TAG_LEN];

  memset(cipher, 0, sizeof(cipher));
  memset(tag, 0, sizeof(tag));

  xoodyak::encrypt(
    XOODYAK_KEY,
    nonce,
    (const uint8_t *)associatedData.c_str(),
    associatedData.length(),
    (const uint8_t *)plaintext.c_str(),
    cipher,
    plaintext.length(),
    tag
  );

  cipherHex = bytesToHex(cipher, plaintext.length());
  tagHex = bytesToHex(tag, TAG_LEN);

  return true;
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(TX_LED_PIN, OUTPUT);
  digitalWrite(TX_LED_PIN, LOW);

  initClockFromCompileTimeWIB();

  Serial.println();
  Serial.println("=== TX INA219 + LoRa + Xoodyak + Timestamp GMT+7 Lokal ===");
  Serial.println("Interval kirim: 4 detik.");
  Serial.println("Serial TX menampilkan TX row, ENC_TX, PLAIN_TX, dan RAW_TX.");
  Serial.println("Dashboard web membaca COM RX, bukan COM TX.");

  Wire.begin(I2C_SDA, I2C_SCL);

  if (!ina219.begin()) {
    Serial.println("INA219 tidak terdeteksi. Cek SDA/SCL/VCC/GND.");
    while (true) {
      delay(1000);
    }
  }

  ina219.setCalibration_32V_2A();
  Serial.println("INA219 siap.");

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
  LoRa.setTxPower(17);

  Serial.println("LoRa TX siap.");
  printTxHeader();
}

void loop() {
  unsigned long nowMs = millis();

  // Scheduler stabil 4 detik.
  if (nextSendMs == 0) {
    nextSendMs = nowMs + SEND_INTERVAL_MS;
    return;
  }

  if ((long)(nowMs - nextSendMs) < 0) {
    return;
  }

  // Jadwal maju tetap 4000 ms.
  // Tidak bergantung pada lama baca sensor/enkripsi/kirim LoRa.
  do {
    nextSendMs += SEND_INTERVAL_MS;
  } while ((long)(nowMs - nextSendMs) >= 0);

  packetCounter++;

  char timeWIB[24];
  getTimestampWIB(timeWIB, sizeof(timeWIB));

  float shuntVoltage_mV = ina219.getShuntVoltage_mV();
  float busVoltage_V = ina219.getBusVoltage_V();
  float current_mA = ina219.getCurrent_mA();
  float power_mW = ina219.getPower_mW();
  float loadVoltage_V = busVoltage_V + (shuntVoltage_mV / 1000.0);

  String plaintext = buildPlaintext(
    loadVoltage_V,
    current_mA,
    power_mW,
    nowMs,
    timeWIB
  );

  uint8_t nonce[NONCE_LEN];
  makeNonce(nonce, packetCounter);

  String nonceHex = bytesToHex(nonce, NONCE_LEN);

  String associatedData = "";
  associatedData.reserve(48);
  associatedData += "ver=1;";
  associatedData += "node=";
  associatedData += String(NODE_ID);
  associatedData += ";";
  associatedData += "ctr=";
  associatedData += String(packetCounter);

  String cipherHex = "";
  String tagHex = "";

  bool ok = encryptPayload(
    associatedData,
    plaintext,
    nonce,
    cipherHex,
    tagHex
  );

  if (!ok) {
    Serial.println("ENC_TX_ERROR | Enkripsi gagal. Payload terlalu panjang atau error.");
    return;
  }

  String packet = "";
  packet.reserve(260);

  packet += associatedData;
  packet += ";";

  packet += "nonce=";
  packet += nonceHex;
  packet += ";";

  packet += "ct=";
  packet += cipherHex;
  packet += ";";

  packet += "tag=";
  packet += tagHex;

  LoRa.beginPacket();
  LoRa.print(packet);
  LoRa.endPacket();

  blinkTxLed();

  printTxRow(
    packetCounter,
    timeWIB,
    nowMs,
    loadVoltage_V,
    current_mA,
    power_mW
  );

  printTxEncryptionRow(
    packetCounter,
    associatedData,
    nonceHex,
    cipherHex,
    tagHex,
    plaintext,
    packet
  );
}