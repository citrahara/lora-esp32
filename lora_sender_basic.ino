/*
  lora_sender_basic.ino

  Prototipe awal komunikasi LoRa tanpa enkripsi.
  Node ini bertindak sebagai Sender / Transmitter.

  Fitur:
  - Membuat 5 packet telemetry normal
  - Mengirim packet JSON melalui LoRa
  - Mengirim 1 packet rusak untuk menguji validasi receiver
  - Mengirim 1 duplicate packet untuk menguji deteksi duplicate

  Library yang dibutuhkan:
  - LoRa by Sandeep Mistry
  - ArduinoJson by Benoit Blanchon

  Catatan:
  - Tidak ada enkripsi
  - Tidak ada authentication tag
  - Tidak ada nonce
  - Tidak ada proteksi anti-replay
*/

#include <SPI.h>
#include <LoRa.h>
#include <ArduinoJson.h>

// =======================
// Konfigurasi dasar node
// =======================

const char* DEVICE_ID = "NODE001";
const uint8_t PACKET_VERSION = 1;
const uint8_t TOTAL_PACKETS = 5;

// Sesuaikan dengan modul dan wilayah penggunaan.
// Contoh umum: 915E6, 923E6, 868E6, 433E6
const long LORA_FREQUENCY = 915E6;

// Dummy LoRa parameter, diset ke radio agar lebih dekat ke implementasi real.
const int SPREADING_FACTOR = 7;
const long BANDWIDTH = 125E3;
const int CODING_RATE_DENOMINATOR = 5;

// =======================
// Pin LoRa
// =======================
// Default untuk Arduino Uno/Nano + SX1276/SX1278/RFM95:
// NSS/CS = D10
// RST    = D9
// DIO0   = D2
//
// Untuk ESP32, contoh umum:
// NSS/CS = GPIO5
// RST    = GPIO14
// DIO0   = GPIO26
// SCK    = GPIO18
// MISO   = GPIO19
// MOSI   = GPIO23

#if defined(ESP32)
const int LORA_SS = 5;
const int LORA_RST = 14;
const int LORA_DIO0 = 26;
const int LORA_SCK = 18;
const int LORA_MISO = 19;
const int LORA_MOSI = 23;
#else
const int LORA_SS = 10;
const int LORA_RST = 9;
const int LORA_DIO0 = 2;
#endif

uint32_t counter = 1;
bool simulationDone = false;

// =======================
// Utility
// =======================

void setupLoRa() {
#if defined(ESP32)
  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_SS);
#endif

  LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);

  if (!LoRa.begin(LORA_FREQUENCY)) {
    Serial.println("[ERROR] LoRa init gagal. Cek wiring, pin, board, dan frekuensi modul.");
    while (true) {
      delay(1000);
    }
  }

  LoRa.setSpreadingFactor(SPREADING_FACTOR);
  LoRa.setSignalBandwidth(BANDWIDTH);
  LoRa.setCodingRate4(CODING_RATE_DENOMINATOR);

  Serial.println("[OK] LoRa sender siap.");
}

float randomFloat(float minValue, float maxValue) {
  long scaledMin = (long)(minValue * 100);
  long scaledMax = (long)(maxValue * 100);
  return random(scaledMin, scaledMax + 1) / 100.0;
}

String makeDummyTimestamp(uint32_t packetCounter) {
  // Arduino tidak punya waktu real tanpa RTC/NTP.
  // Ini timestamp dummy agar format tetap menyerupai ISO-8601.
  char buffer[25];
  uint8_t second = packetCounter % 60;
  snprintf(buffer, sizeof(buffer), "2026-05-26T10:00:%02u", second);
  return String(buffer);
}

String buildTelemetryPacket(uint32_t packetCounter) {
  StaticJsonDocument<512> doc;

  doc["version"] = PACKET_VERSION;
  doc["device_id"] = DEVICE_ID;
  doc["counter"] = packetCounter;

  JsonObject payload = doc.createNestedObject("payload");
  payload["temperature"] = randomFloat(25.0, 34.0);
  payload["humidity"] = randomFloat(55.0, 85.0);
  payload["battery"] = randomFloat(70.0, 100.0);
  payload["timestamp"] = makeDummyTimestamp(packetCounter);

  String packet;
  serializeJson(doc, packet);
  return packet;
}

void sendRawPacket(const String& rawPacket, const char* label) {
  Serial.print("[SENDER] ");
  Serial.print(label);
  Serial.print(" | raw=");
  Serial.println(rawPacket);

  LoRa.beginPacket();
  LoRa.print(rawPacket);
  LoRa.endPacket();

  Serial.println("[CHANNEL] Packet dikirim via LoRa.");
  delay(1000);
}

// =======================
// Arduino lifecycle
// =======================

void setup() {
  Serial.begin(9600);
  while (!Serial) {
    ; // Untuk board tertentu yang butuh Serial siap dulu.
  }

  randomSeed(analogRead(A0));

  Serial.println("=== LORA BASIC TELEMETRY SENDER ===");
  Serial.println("Security: OFF / tanpa enkripsi / tanpa authentication tag");

  setupLoRa();
}

void loop() {
  if (simulationDone) {
    return;
  }

  Serial.println();
  Serial.println("=== SKENARIO 1: Kirim 5 packet normal ===");

  for (uint8_t i = 0; i < TOTAL_PACKETS; i++) {
    String packet = buildTelemetryPacket(counter);
    sendRawPacket(packet, "Packet normal dibuat");
    counter++;
  }

  Serial.println();
  Serial.println("=== SKENARIO 3: Kirim 1 packet rusak ===");

  String corruptedPacket = "{\"version\":1,\"device_id\":\"NODE001\",\"counter\":6,\"payload\":";
  sendRawPacket(corruptedPacket, "Packet rusak dibuat");

  Serial.println();
  Serial.println("=== SKENARIO 5: Kirim duplicate packet ===");

  String duplicatePacket = buildTelemetryPacket(counter);
  sendRawPacket(duplicatePacket, "Packet duplicate pertama");
  delay(500);
  sendRawPacket(duplicatePacket, "Packet duplicate kedua");

  counter++;
  simulationDone = true;

  Serial.println();
  Serial.println("=== SENDER SELESAI ===");
}
