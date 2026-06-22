#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <LoRa.h>
#include <Adafruit_INA219.h>
#include <cstring>
#include <time.h>
#include <sys/time.h>

// =============================================================
// TX: INA219 -> plaintext sensor -> LoRa packet
// Tanpa Xoodyak, tanpa nonce, tanpa ciphertext, tanpa tag.
// Payload dikirim langsung dalam format key=value dipisah ';'.
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

Adafruit_INA219 ina219;

uint32_t packetCounter = 0;
unsigned long nextSendMs = 0;
bool txHeaderPrinted = false;

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

String buildPlainPacket(
  uint32_t ctr,
  float voltageV,
  float currentmA,
  float powermW,
  unsigned long sentMs,
  const char *timeWIB
) {
  String payload;
  payload.reserve(128);

  payload += "ver=1;";
  payload += "node=";
  payload += NODE_ID;
  payload += ";ctr=";
  payload += String(ctr);
  payload += ";rt=";
  payload += timeWIB;
  payload += ";ts=";
  payload += String(sentMs);
  payload += ";v=";
  payload += String(voltageV, 3);
  payload += ";i=";
  payload += String(currentmA, 2);
  payload += ";p=";
  payload += String(powermW, 2);

  return payload;
}

void printTxHeader() {
  Serial.println();
  Serial.println("================================================================================================");
  Serial.println("DIR | NODE |   CTR | TIME GMT+7         |      TXms |    V(V) |    I(mA) |    P(mW) | STATUS");
  Serial.println("------------------------------------------------------------------------------------------------");
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

void blinkTxLed() {
  digitalWrite(TX_LED_PIN, HIGH);
  delay(40);
  digitalWrite(TX_LED_PIN, LOW);
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(TX_LED_PIN, OUTPUT);
  digitalWrite(TX_LED_PIN, LOW);

  initClockFromCompileTimeWIB();

  Serial.println();
  Serial.println("=== TX INA219 + LoRa Plaintext + Timestamp GMT+7 Lokal ===");
  Serial.println("Interval kirim: 4 detik.");
  Serial.println("Payload dikirim tanpa enkripsi. Gunakan hanya untuk pengujian atau jaringan yang tidak membutuhkan kerahasiaan.");

  Wire.begin(I2C_SDA, I2C_SCL);

  if (!ina219.begin()) {
    Serial.println("INA219 tidak terdeteksi. Cek SDA/SCL/VCC/GND.");
    while (true) delay(1000);
  }

  ina219.setCalibration_32V_2A();
  Serial.println("INA219 siap.");

  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_SS);
  LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);

  if (!LoRa.begin(LORA_FREQ)) {
    Serial.println("LoRa init gagal. Cek wiring, power 3.3V, antena, dan frekuensi.");
    while (true) delay(1000);
  }

  LoRa.setSpreadingFactor(7);
  LoRa.setSignalBandwidth(125E3);
  LoRa.setCodingRate4(5);
  LoRa.setTxPower(17);
  LoRa.enableCrc();

  Serial.println("LoRa TX plaintext siap.");
  printTxHeader();
}

void loop() {
  const unsigned long nowMs = millis();

  if (nextSendMs == 0) {
    nextSendMs = nowMs + SEND_INTERVAL_MS;
    return;
  }

  if ((long)(nowMs - nextSendMs) < 0) {
    return;
  }

  do {
    nextSendMs += SEND_INTERVAL_MS;
  } while ((long)(nowMs - nextSendMs) >= 0);

  packetCounter++;

  char timeWIB[24];
  getTimestampWIB(timeWIB, sizeof(timeWIB));

  const float shuntVoltage_mV = ina219.getShuntVoltage_mV();
  const float busVoltage_V = ina219.getBusVoltage_V();
  const float current_mA = ina219.getCurrent_mA();
  const float power_mW = ina219.getPower_mW();
  const float loadVoltage_V = busVoltage_V + (shuntVoltage_mV / 1000.0);

  const String packet = buildPlainPacket(
    packetCounter,
    loadVoltage_V,
    current_mA,
    power_mW,
    nowMs,
    timeWIB
  );

  LoRa.beginPacket();
  LoRa.print(packet);
  LoRa.endPacket();

  blinkTxLed();

  printTxRow(packetCounter, timeWIB, nowMs, loadVoltage_V, current_mA, power_mW);
  Serial.print("PLAIN_TX | ");
  Serial.println(packet);
  Serial.print("RAW_TX | ");
  Serial.println(packet);
}
