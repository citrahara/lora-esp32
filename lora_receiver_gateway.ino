/*
  lora_receiver_gateway.ino

  Prototipe awal komunikasi LoRa tanpa enkripsi.
  Node ini bertindak sebagai Receiver / Gateway.

  Fitur:
  - Menerima packet JSON melalui LoRa
  - Parsing packet
  - Validasi field wajib:
    version, device_id, counter, payload
  - Validasi field payload:
    temperature, humidity, battery, timestamp
  - Menampilkan telemetry jika valid
  - Menolak packet rusak / format tidak sesuai
  - Mendeteksi duplicate packet, tetapi belum menolak sebagai keamanan

  Library yang dibutuhkan:
  - LoRa by Sandeep Mistry
  - ArduinoJson by Benoit Blanchon

  Catatan:
  - Tidak ada enkripsi
  - Tidak ada authentication tag
  - Tidak ada nonce
  - Duplicate hanya dideteksi, belum diblokir
*/

#include <SPI.h>
#include <LoRa.h>
#include <ArduinoJson.h>

// =======================
// Konfigurasi dasar gateway
// =======================

const uint8_t PACKET_VERSION = 1;

// Sesuaikan dengan sender.
const long LORA_FREQUENCY = 915E6;

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

// Buffer sederhana untuk deteksi duplicate.
// Untuk prototipe cukup simpan 10 packet terakhir.
const uint8_t RECENT_PACKET_SIZE = 10;
String recentDeviceIds[RECENT_PACKET_SIZE];
uint32_t recentCounters[RECENT_PACKET_SIZE];
uint8_t recentIndex = 0;

// =======================
// Utility LoRa
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

  Serial.println("[OK] LoRa receiver/gateway siap.");
}

bool isDuplicatePacket(const String& deviceId, uint32_t counter) {
  for (uint8_t i = 0; i < RECENT_PACKET_SIZE; i++) {
    if (recentDeviceIds[i] == deviceId && recentCounters[i] == counter) {
      return true;
    }
  }

  recentDeviceIds[recentIndex] = deviceId;
  recentCounters[recentIndex] = counter;
  recentIndex = (recentIndex + 1) % RECENT_PACKET_SIZE;

  return false;
}

bool validatePacket(StaticJsonDocument<512>& doc, String& reason) {
  if (!doc.containsKey("version")) {
    reason = "Field wajib hilang: version";
    return false;
  }

  if (!doc.containsKey("device_id")) {
    reason = "Field wajib hilang: device_id";
    return false;
  }

  if (!doc.containsKey("counter")) {
    reason = "Field wajib hilang: counter";
    return false;
  }

  if (!doc.containsKey("payload")) {
    reason = "Field wajib hilang: payload";
    return false;
  }

  int version = doc["version"].as<int>();
  if (version != PACKET_VERSION) {
    reason = "Versi packet tidak didukung";
    return false;
  }

  const char* deviceId = doc["device_id"] | "";
  if (strlen(deviceId) == 0) {
    reason = "device_id kosong atau bukan string";
    return false;
  }

  if (!doc["counter"].is<uint32_t>()) {
    reason = "counter harus integer positif";
    return false;
  }

  uint32_t counter = doc["counter"].as<uint32_t>();
  if (counter < 1) {
    reason = "counter harus lebih dari 0";
    return false;
  }

  JsonVariant payload = doc["payload"];
  if (!payload.is<JsonObject>()) {
    reason = "payload harus object";
    return false;
  }

  if (!payload["temperature"].is<float>() && !payload["temperature"].is<int>()) {
    reason = "payload.temperature hilang atau bukan angka";
    return false;
  }

  if (!payload["humidity"].is<float>() && !payload["humidity"].is<int>()) {
    reason = "payload.humidity hilang atau bukan angka";
    return false;
  }

  if (!payload["battery"].is<float>() && !payload["battery"].is<int>()) {
    reason = "payload.battery hilang atau bukan angka";
    return false;
  }

  const char* timestamp = payload["timestamp"] | "";
  if (strlen(timestamp) == 0) {
    reason = "payload.timestamp hilang atau kosong";
    return false;
  }

  reason = "Packet valid";
  return true;
}

void displayTelemetry(StaticJsonDocument<512>& doc) {
  const char* deviceId = doc["device_id"];
  uint32_t counter = doc["counter"].as<uint32_t>();

  JsonObject payload = doc["payload"];
  float temperature = payload["temperature"].as<float>();
  float humidity = payload["humidity"].as<float>();
  float battery = payload["battery"].as<float>();
  const char* timestamp = payload["timestamp"];

  Serial.println("[TELEMETRY]");
  Serial.print("  device_id   : ");
  Serial.println(deviceId);
  Serial.print("  counter     : ");
  Serial.println(counter);
  Serial.print("  temperature : ");
  Serial.print(temperature);
  Serial.println(" C");
  Serial.print("  humidity    : ");
  Serial.print(humidity);
  Serial.println(" %");
  Serial.print("  battery     : ");
  Serial.print(battery);
  Serial.println(" %");
  Serial.print("  timestamp   : ");
  Serial.println(timestamp);
}

void processRawPacket(const String& rawPacket) {
  Serial.println();
  Serial.print("[RECEIVER] Packet diterima | raw=");
  Serial.println(rawPacket);

  StaticJsonDocument<512> doc;
  DeserializationError error = deserializeJson(doc, rawPacket);

  if (error) {
    Serial.print("[RECEIVER] Packet ditolak | alasan=JSON tidak valid: ");
    Serial.println(error.c_str());
    return;
  }

  String reason;
  if (!validatePacket(doc, reason)) {
    Serial.print("[RECEIVER] Packet ditolak | alasan=");
    Serial.println(reason);
    return;
  }

  String deviceId = doc["device_id"].as<String>();
  uint32_t counter = doc["counter"].as<uint32_t>();

  bool duplicate = isDuplicatePacket(deviceId, counter);
  if (duplicate) {
    Serial.print("[RECEIVER] Duplicate packet terdeteksi | device_id=");
    Serial.print(deviceId);
    Serial.print(" counter=");
    Serial.println(counter);
  }

  Serial.print("[RECEIVER] Packet valid | ");
  Serial.println(reason);

  displayTelemetry(doc);
}

// =======================
// Arduino lifecycle
// =======================

void setup() {
  Serial.begin(9600);
  while (!Serial) {
    ; // Untuk board tertentu yang butuh Serial siap dulu.
  }

  Serial.println("=== LORA BASIC TELEMETRY RECEIVER / GATEWAY ===");
  Serial.println("Security: OFF / tanpa enkripsi / tanpa authentication tag");

  setupLoRa();
}

void loop() {
  int packetSize = LoRa.parsePacket();

  if (packetSize == 0) {
    return;
  }

  String rawPacket = "";

  while (LoRa.available()) {
    rawPacket += (char)LoRa.read();
  }

  Serial.print("[RADIO] RSSI=");
  Serial.print(LoRa.packetRssi());
  Serial.print(" dBm | SNR=");
  Serial.println(LoRa.packetSnr());

  processRawPacket(rawPacket);
}
