#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <LoRa.h>
#include <Adafruit_INA219.h>
#include <cstring>

#include "xoodyak.hpp"

// =============================================================
// TX: INA219 -> plaintext sensor -> Xoodyak AEAD -> LoRa packet
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
// Pin I2C INA219
// -----------------------
#define I2C_SDA 8
#define I2C_SCL 9

// -----------------------
// LED indikator pengiriman
// Banyak ESP32-S3 memakai GPIO48 untuk RGB/builtin LED.
// Kalau LED tidak menyala, ganti ke pin LED board kamu.
// -----------------------
#ifndef LED_BUILTIN
#define LED_BUILTIN 48
#endif
#define TX_LED_PIN LED_BUILTIN

// -----------------------
// Konfigurasi aplikasi
// -----------------------
#define NODE_ID "K1"
#define SEND_INTERVAL_MS 3000

#define KEY_LEN   16
#define NONCE_LEN 16
#define TAG_LEN   16
#define MAX_TEXT_LEN 128

Adafruit_INA219 ina219;

uint32_t packetCounter = 0;
unsigned long lastSendMs = 0;

// Key demo 128-bit. TX dan RX harus sama.
// Catatan manusiawi: jangan pakai key demo ini untuk sistem nyata. Dunia sudah cukup kacau.
uint8_t XOODYAK_KEY[KEY_LEN] = {
  0x10, 0x21, 0x32, 0x43,
  0x54, 0x65, 0x76, 0x87,
  0x98, 0xA9, 0xBA, 0xCB,
  0xDC, 0xED, 0xFE, 0x0F
};

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

  // 4 byte counter agar nonce mudah ditelusuri.
  nonce[0] = (counter >> 24) & 0xFF;
  nonce[1] = (counter >> 16) & 0xFF;
  nonce[2] = (counter >> 8) & 0xFF;
  nonce[3] = counter & 0xFF;

  // 2 byte node ID sederhana.
  nonce[4] = NODE_ID[0];
  nonce[5] = NODE_ID[1];

  // 10 byte random dari ESP32.
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

String buildPlaintext(float voltageV, float currentmA, float powermW, unsigned long sentMs) {
  // ts = timestamp relatif dari sumber/TX dalam millisecond sejak TX menyala.
  // Format sengaja key=value; supaya parsing RX tidak berubah menjadi ritual pemanggilan setan.
  String payload = "";
  payload.reserve(90);
  payload += "dev=";
  payload += String(NODE_ID);
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

  Serial.println();
  Serial.println("=== TX INA219 + LoRa + Xoodyak + Timestamp ===");

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
}

void loop() {
  unsigned long nowMs = millis();

  if (nowMs - lastSendMs < SEND_INTERVAL_MS) {
    return;
  }

  lastSendMs = nowMs;
  packetCounter++;

  float shuntVoltage_mV = ina219.getShuntVoltage_mV();
  float busVoltage_V = ina219.getBusVoltage_V();
  float current_mA = ina219.getCurrent_mA();
  float power_mW = ina219.getPower_mW();
  float loadVoltage_V = busVoltage_V + (shuntVoltage_mV / 1000.0);

  String plaintext = buildPlaintext(loadVoltage_V, current_mA, power_mW, nowMs);

  uint8_t nonce[NONCE_LEN];
  makeNonce(nonce, packetCounter);

  String associatedData = "";
  associatedData.reserve(40);
  associatedData += "ver=1;";
  associatedData += "node=";
  associatedData += String(NODE_ID);
  associatedData += ";";
  associatedData += "ctr=";
  associatedData += String(packetCounter);

  String cipherHex = "";
  String tagHex = "";

  bool ok = encryptPayload(associatedData, plaintext, nonce, cipherHex, tagHex);

  if (!ok) {
    Serial.println("Enkripsi gagal. Payload terlalu panjang atau error.");
    return;
  }

  String packet = "";
  packet.reserve(230);
  packet += associatedData;
  packet += ";";
  packet += "nonce=";
  packet += bytesToHex(nonce, NONCE_LEN);
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

  Serial.println();
  Serial.println("===== TX SENSOR =====");
  Serial.print("TX timestamp : ");
  Serial.print(nowMs);
  Serial.println(" ms sejak boot");
  Serial.print("Packet ctr   : ");
  Serial.println(packetCounter);
  Serial.print("Voltage      : ");
  Serial.print(loadVoltage_V, 3);
  Serial.println(" V");
  Serial.print("Current      : ");
  Serial.print(current_mA, 2);
  Serial.println(" mA");
  Serial.print("Power        : ");
  Serial.print(power_mW, 2);
  Serial.println(" mW");
  Serial.println("Plaintext:");
  Serial.println(plaintext);
  Serial.println("Paket LoRa terenkripsi:");
  Serial.println(packet);
  Serial.println("Status: paket terkirim, LED indikator berkedip.");
}
