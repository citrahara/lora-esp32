#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <LoRa.h>
#include <Adafruit_INA219.h>
#include <cstring>

#include "xoodyak.hpp"

// =======================
// Pin LoRa ESP32-S3
// =======================
#define LORA_SCK   12
#define LORA_MISO  13
#define LORA_MOSI  11
#define LORA_SS    10
#define LORA_RST   5
#define LORA_DIO0  4

#define LORA_FREQ  433E6

// =======================
// Pin I2C INA219
// =======================
#define I2C_SDA 8
#define I2C_SCL 9

// =======================
// Konfigurasi
// =======================
#define NODE_ID "K1"
#define SEND_INTERVAL_MS 3000

#define KEY_LEN   16
#define NONCE_LEN 16
#define TAG_LEN   16

#define MAX_TEXT_LEN 96

Adafruit_INA219 ina219;

uint32_t packetCounter = 0;
unsigned long lastSend = 0;

// Key demo 128-bit.
// TX dan RX harus sama.
uint8_t XOODYAK_KEY[KEY_LEN] = {
  0x10, 0x21, 0x32, 0x43,
  0x54, 0x65, 0x76, 0x87,
  0x98, 0xA9, 0xBA, 0xCB,
  0xDC, 0xED, 0xFE, 0x0F
};

String bytesToHex(const uint8_t *data, size_t len) {
  const char hexChars[] = "0123456789ABCDEF";
  String result = "";

  for (size_t i = 0; i < len; i++) {
    result += hexChars[(data[i] >> 4) & 0x0F];
    result += hexChars[data[i] & 0x0F];
  }

  return result;
}

void makeNonce(uint8_t nonce[NONCE_LEN], uint32_t counter) {
  memset(nonce, 0, NONCE_LEN);

  nonce[0] = (counter >> 24) & 0xFF;
  nonce[1] = (counter >> 16) & 0xFF;
  nonce[2] = (counter >> 8) & 0xFF;
  nonce[3] = counter & 0xFF;

  nonce[4] = 'K';
  nonce[5] = '1';

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

String buildPlaintext(float voltageV, float currentmA, float powermW) {
  String payload = "";
  payload += "dev=" + String(NODE_ID) + ";";
  payload += "v=" + String(voltageV, 3) + ";";
  payload += "i=" + String(currentmA, 2) + ";";
  payload += "p=" + String(powermW, 2);

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

  Serial.println();
  Serial.println("=== TX INA219 + LoRa + Xoodyak.hpp ===");

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
  unsigned long now = millis();

  if (now - lastSend < SEND_INTERVAL_MS) {
    return;
  }

  lastSend = now;
  packetCounter++;

  float shuntVoltage_mV = ina219.getShuntVoltage_mV();
  float busVoltage_V = ina219.getBusVoltage_V();
  float current_mA = ina219.getCurrent_mA();
  float power_mW = ina219.getPower_mW();

  float loadVoltage_V = busVoltage_V + (shuntVoltage_mV / 1000.0);

  String plaintext = buildPlaintext(loadVoltage_V, current_mA, power_mW);

  uint8_t nonce[NONCE_LEN];
  makeNonce(nonce, packetCounter);

  String nonceHex = bytesToHex(nonce, NONCE_LEN);

  String associatedData = "";
  associatedData += "ver=1;";
  associatedData += "node=" + String(NODE_ID) + ";";
  associatedData += "ctr=" + String(packetCounter);

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
    Serial.println("Enkripsi gagal. Payload terlalu panjang atau error.");
    return;
  }

  String packet = "";
  packet += associatedData + ";";
  packet += "nonce=" + nonceHex + ";";
  packet += "ct=" + cipherHex + ";";
  packet += "tag=" + tagHex;

  Serial.println();
  Serial.println("===== TX SENSOR =====");

  Serial.print("Voltage : ");
  Serial.print(loadVoltage_V, 3);
  Serial.println(" V");

  Serial.print("Current : ");
  Serial.print(current_mA, 2);
  Serial.println(" mA");

  Serial.print("Power   : ");
  Serial.print(power_mW, 2);
  Serial.println(" mW");

  Serial.println();
  Serial.println("Plaintext:");
  Serial.println(plaintext);

  Serial.println();
  Serial.println("Ciphertext HEX:");
  Serial.println(cipherHex);

  Serial.println();
  Serial.println("Tag HEX:");
  Serial.println(tagHex);

  Serial.println();
  Serial.println("Paket LoRa:");
  Serial.println(packet);

  LoRa.beginPacket();
  LoRa.print(packet);
  LoRa.endPacket();

  Serial.println("Status: paket terenkripsi terkirim.");
}