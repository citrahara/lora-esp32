#include <Arduino.h>
#include <SPI.h>
#include <LoRa.h>
#include <WiFi.h>
#include <WebServer.h>
#include <cstring>

#include "xoodyak.hpp"
#include "DashboardUI.h"

// =============================================================
// RX: LoRa packet -> Xoodyak verified decrypt -> API + Dashboard
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
// Banyak ESP32-S3 memakai GPIO48 untuk RGB/builtin LED.
// Kalau LED tidak menyala, ganti ke pin LED board kamu.
// -----------------------
#ifndef LED_BUILTIN
#define LED_BUILTIN 48
#endif
#define RX_LED_PIN LED_BUILTIN

// -----------------------
// WiFi dashboard lokal
// -----------------------
const char *AP_SSID = "LoRa-RX-Dashboard";
const char *AP_PASS = "12345678";
WebServer server(80);

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

// -----------------------
// Data dashboard
// -----------------------
struct SensorRecord {
  bool valid = false;
  String status = "Belum ada paket";
  String node = "-";
  uint32_t counter = 0;
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

const int HISTORY_SIZE = 40;
SensorRecord historyBuffer[HISTORY_SIZE];
int historyHead = 0;
int historyCount = 0;

SensorRecord latest;
uint32_t totalPackets = 0;
uint32_t validPackets = 0;
uint32_t failedPackets = 0;

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

      // Cocokkan key penuh. Jangan pakai indexOf("v=") karena nanti dev=K1 ikut keseret.
      if (tokenKey == key) return tokenValue;
    }

    pos = sep + 1;
  }

  return "";
}

String jsonEscape(String s) {
  s.replace("\\", "\\\\");
  s.replace("\"", "\\\"");
  s.replace("\n", "\\n");
  s.replace("\r", "\\r");
  s.replace("\t", "\\t");
  return s;
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

void pushHistory(const SensorRecord &record) {
  historyBuffer[historyHead] = record;
  historyHead = (historyHead + 1) % HISTORY_SIZE;
  if (historyCount < HISTORY_SIZE) historyCount++;
}

String recordToJson(const SensorRecord &r) {
  String json = "";
  json.reserve(420);
  json += "{";
  json += "\"valid\":";
  json += (r.valid ? "true" : "false");
  json += ",";
  json += "\"status\":\"";
  json += jsonEscape(r.status);
  json += "\",";
  json += "\"node\":\"";
  json += jsonEscape(r.node);
  json += "\",";
  json += "\"counter\":";
  json += String(r.counter);
  json += ",";
  json += "\"tx_ms\":";
  json += String(r.txMs);
  json += ",";
  json += "\"rx_ms\":";
  json += String(r.rxMs);
  json += ",";
  json += "\"voltage\":";
  json += String(r.voltage, 3);
  json += ",";
  json += "\"current\":";
  json += String(r.current, 2);
  json += ",";
  json += "\"power\":";
  json += String(r.power, 2);
  json += ",";
  json += "\"rssi\":";
  json += String(r.rssi);
  json += ",";
  json += "\"snr\":";
  json += String(r.snr, 2);
  json += ",";
  json += "\"plaintext\":\"";
  json += jsonEscape(r.plaintext);
  json += "\",";
  json += "\"raw\":\"";
  json += jsonEscape(r.raw);
  json += "\"";
  json += "}";
  return json;
}

String buildDataJson() {
  String json = "";
  json.reserve(9000);
  json += "{";
  json += "\"status\":\"";
  json += jsonEscape(latest.status);
  json += "\",";
  json += "\"total_packets\":";
  json += String(totalPackets);
  json += ",";
  json += "\"valid_packets\":";
  json += String(validPackets);
  json += ",";
  json += "\"failed_packets\":";
  json += String(failedPackets);
  json += ",";
  json += "\"uptime_ms\":";
  json += String(millis());
  json += ",";
  json += "\"latest\":";
  json += recordToJson(latest);
  json += ",";
  json += "\"history\":[";

  int start = (historyHead - historyCount + HISTORY_SIZE) % HISTORY_SIZE;
  for (int i = 0; i < historyCount; i++) {
    int idx = (start + i) % HISTORY_SIZE;
    if (i > 0) json += ",";
    json += recordToJson(historyBuffer[idx]);
  }

  json += "]}";
  return json;
}

void handleRoot() {
  server.send_P(200, "text/html", DASHBOARD_HTML);
}

void handleApiData() {
  server.send(200, "application/json", buildDataJson());
}

void handleNotFound() {
  server.send(404, "application/json", "{\"error\":\"endpoint tidak ditemukan\"}");
}

void markFailure(const String &status, const String &packet, int rssi, float snr) {
  failedPackets++;
  latest.valid = false;
  latest.status = status;
  latest.rxMs = millis();
  latest.rssi = rssi;
  latest.snr = snr;
  latest.raw = packet;
  latest.plaintext = "-";
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
    markFailure("Format paket invalid", packet, rssi, snr);
    Serial.println("Format paket invalid.");
    return;
  }

  uint8_t nonce[NONCE_LEN];
  size_t nonceLen = 0;
  bool nonceOk = hexToBytes(nonceHex, nonce, NONCE_LEN, nonceLen);

  if (!nonceOk || nonceLen != NONCE_LEN) {
    markFailure("Nonce invalid", packet, rssi, snr);
    Serial.println("Nonce invalid.");
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
    markFailure("Dekripsi gagal", packet, rssi, snr);
    Serial.println("Dekripsi gagal. Key, nonce, AD, ciphertext, atau tag tidak cocok.");
    return;
  }

  SensorRecord rec;
  rec.valid = true;
  rec.status = "Paket valid";
  rec.node = node;
  rec.counter = ctr.toInt();
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
  pushHistory(rec);
  blinkRxLed();

  Serial.println();
  Serial.println("===== RX PAKET VALID =====");
  Serial.print("Node       : "); Serial.println(rec.node);
  Serial.print("Counter    : "); Serial.println(rec.counter);
  Serial.print("TX time    : "); Serial.print(rec.txMs); Serial.println(" ms");
  Serial.print("RX time    : "); Serial.print(rec.rxMs); Serial.println(" ms");
  Serial.print("Voltage    : "); Serial.print(rec.voltage, 3); Serial.println(" V");
  Serial.print("Current    : "); Serial.print(rec.current, 2); Serial.println(" mA");
  Serial.print("Power      : "); Serial.print(rec.power, 2); Serial.println(" mW");
  Serial.print("RSSI/SNR   : "); Serial.print(rec.rssi); Serial.print(" / "); Serial.println(rec.snr, 2);
  Serial.println("Plaintext  :"); Serial.println(rec.plaintext);
  Serial.print("Dashboard  : http://"); Serial.println(WiFi.softAPIP());
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(RX_LED_PIN, OUTPUT);
  digitalWrite(RX_LED_PIN, LOW);

  Serial.println();
  Serial.println("=== RX LoRa + Xoodyak + Dashboard Terpisah ===");

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

  Serial.println("LoRa RX siap.");

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);

  server.on("/", handleRoot);
  server.on("/api/data", handleApiData);
  server.onNotFound(handleNotFound);
  server.begin();

  Serial.println();
  Serial.println("Dashboard WiFi aktif.");
  Serial.print("SSID     : "); Serial.println(AP_SSID);
  Serial.print("Password : "); Serial.println(AP_PASS);
  Serial.print("URL      : http://"); Serial.println(WiFi.softAPIP());
  Serial.println("Menunggu paket LoRa...");
}

void loop() {
  server.handleClient();

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
