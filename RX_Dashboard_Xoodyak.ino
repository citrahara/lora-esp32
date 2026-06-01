#include <Arduino.h>
#include <SPI.h>
#include <LoRa.h>
#include <WiFi.h>
#include <WebServer.h>
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
// WiFi Dashboard Lokal
// =======================
const char *AP_SSID = "LoRa-RX-Dashboard";
const char *AP_PASS = "12345678";

WebServer server(80);

// =======================
// Xoodyak config
// =======================
#define KEY_LEN   16
#define NONCE_LEN 16
#define TAG_LEN   16

#define MAX_TEXT_LEN 96

uint8_t XOODYAK_KEY[KEY_LEN] = {
  0x10, 0x21, 0x32, 0x43,
  0x54, 0x65, 0x76, 0x87,
  0x98, 0xA9, 0xBA, 0xCB,
  0xDC, 0xED, 0xFE, 0x0F
};

// =======================
// Data dashboard
// =======================
String lastStatus = "Belum ada paket";
String lastPacket = "-";
String lastCipherHex = "-";
String lastTagHex = "-";
String lastPlaintext = "-";
String lastNode = "-";
String lastCounter = "-";
String lastRssi = "-";
String lastSnr = "-";

float lastVoltage = 0.0;
float lastCurrent = 0.0;
float lastPower = 0.0;

int hexValue(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

bool hexToBytes(
  const String &hex,
  uint8_t *out,
  size_t maxOutLen,
  size_t &outLen
) {
  if (hex.length() % 2 != 0) {
    return false;
  }

  size_t byteLen = hex.length() / 2;

  if (byteLen > maxOutLen) {
    return false;
  }

  for (size_t i = 0; i < byteLen; i++) {
    int high = hexValue(hex[i * 2]);
    int low = hexValue(hex[i * 2 + 1]);

    if (high < 0 || low < 0) {
      return false;
    }

    out[i] = (uint8_t)((high << 4) | low);
  }

  outLen = byteLen;
  return true;
}

String getField(const String &src, const String &key) {
  // Ambil field berdasarkan token penuh yang dipisah tanda ';'.
  // Ini penting karena key pendek seperti "v" bisa salah terbaca dari "dev=K1".
  int pos = 0;

  while (pos < src.length()) {
    int sep = src.indexOf(';', pos);

    if (sep < 0) {
      sep = src.length();
    }

    String token = src.substring(pos, sep);
    token.trim();

    int eq = token.indexOf('=');

    if (eq > 0) {
      String tokenKey = token.substring(0, eq);
      String tokenValue = token.substring(eq + 1);

      tokenKey.trim();
      tokenValue.trim();

      if (tokenKey == key) {
        return tokenValue;
      }
    }

    pos = sep + 1;
  }

  return "";
}

String htmlEscape(String s) {
  s.replace("&", "&amp;");
  s.replace("<", "&lt;");
  s.replace(">", "&gt;");
  s.replace("\"", "&quot;");
  return s;
}

void parsePlaintextSensor(const String &plaintext) {
  String v = getField(plaintext, "v");
  String i = getField(plaintext, "i");
  String p = getField(plaintext, "p");

  if (v.length() > 0) {
    lastVoltage = v.toFloat();
  }

  if (i.length() > 0) {
    lastCurrent = i.toFloat();
  }

  if (p.length() > 0) {
    lastPower = p.toFloat();
  }
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

  if (!cipherOk || !tagOk) {
    return false;
  }

  if (tagLen != TAG_LEN) {
    return false;
  }

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

String dashboardPage() {
  String ip = WiFi.softAPIP().toString();

  String page = "";

  page += "<!DOCTYPE html><html><head>";
  page += "<meta charset='UTF-8'>";
  page += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  page += "<meta http-equiv='refresh' content='2'>";
  page += "<title>LoRa RX Dashboard</title>";

  page += "<style>";
  page += ":root{--bg:#07111f;--card:#101c2b;--line:#24364a;--text:#f8fafc;--muted:#94a3b8;--cyan:#22d3ee;--green:#22c55e;--purple:#8b5cf6;--yellow:#facc15;}";
  page += "*{box-sizing:border-box}";
  page += "body{margin:0;font-family:Arial,Helvetica,sans-serif;background:radial-gradient(circle at top left,#10223a,#07111f 50%,#030712);color:var(--text);padding:22px;}";
  page += ".wrap{max-width:1200px;margin:auto}";
  page += ".top{display:flex;justify-content:space-between;align-items:center;gap:14px;margin-bottom:22px}";
  page += "h1{margin:0;font-size:36px;letter-spacing:-1px}";
  page += ".sub{margin-top:8px;color:#cbd5e1;font-size:16px}.sub b{color:var(--cyan)}";
  page += ".badge{background:rgba(16,28,43,.85);border:1px solid var(--line);border-radius:14px;padding:14px 18px;min-width:230px}";
  page += ".dot{display:inline-block;width:12px;height:12px;background:var(--green);border-radius:50%;margin-right:8px;box-shadow:0 0 14px var(--green)}";
  page += ".status{display:grid;grid-template-columns:repeat(5,1fr);border:1px solid var(--line);border-radius:16px;overflow:hidden;margin-bottom:18px;background:rgba(16,28,43,.72)}";
  page += ".stat{padding:18px;border-right:1px solid var(--line)}.stat:last-child{border-right:0}.label{color:var(--muted);font-size:13px}.val{font-size:20px;font-weight:bold;margin-top:8px}";
  page += ".green{color:var(--green)}.cyan{color:var(--cyan)}.purple{color:var(--purple)}.yellow{color:var(--yellow)}";
  page += ".metrics{display:grid;grid-template-columns:repeat(3,1fr);gap:16px;margin-bottom:16px}";
  page += ".metric{background:linear-gradient(145deg,rgba(16,28,43,.95),rgba(11,22,36,.95));border:1px solid var(--line);border-radius:18px;padding:22px;min-height:145px}";
  page += ".metric h2{margin:0;color:#cbd5e1;font-size:18px}.num{font-size:42px;font-weight:800;margin-top:18px}.unit{font-size:18px;color:var(--muted);margin-left:6px}";
  page += ".spark{height:34px;margin-top:18px;border-radius:10px;background:linear-gradient(90deg,rgba(34,211,238,.18),rgba(34,197,94,.12),rgba(139,92,246,.13));border:1px solid rgba(148,163,184,.13)}";
  page += ".panels{display:grid;grid-template-columns:1fr 1fr 1fr;gap:16px}";
  page += ".panel{background:rgba(16,28,43,.88);border:1px solid var(--line);border-radius:16px;padding:16px;min-height:230px}";
  page += ".panel h3{margin:0 0 12px 0;font-size:17px}.tag{float:right;color:var(--cyan);font-size:13px}";
  page += "pre{white-space:pre-wrap;word-break:break-word;background:#030712;border:1px solid #1f2937;border-radius:12px;padding:14px;color:#dbeafe;font-size:13px;line-height:1.55;min-height:150px}";
  page += ".plain{color:#86efac}.hex{color:#7dd3fc}.raw{color:#e5e7eb}";
  page += ".foot{margin-top:16px;border:1px solid var(--line);background:rgba(16,28,43,.8);border-radius:16px;padding:14px 18px;display:flex;justify-content:space-between;color:#cbd5e1}";
  page += "@media(max-width:900px){.top,.foot{flex-direction:column;align-items:flex-start}.status,.metrics,.panels{grid-template-columns:1fr}.stat{border-right:0;border-bottom:1px solid var(--line)}}";
  page += "</style>";

  page += "</head><body><div class='wrap'>";

  page += "<div class='top'>";
  page += "<div>";
  page += "<h1>LoRa RX Dashboard</h1>";
  page += "<div class='sub'>Monitoring hasil sensor terenkripsi <b>Xoodyak</b></div>";
  page += "</div>";
  page += "<div class='badge'><span class='dot'></span>Dashboard Lokal<br><span class='cyan'>http://" + ip + "</span></div>";
  page += "</div>";

  page += "<div class='status'>";
  page += "<div class='stat'><div class='label'>Status Paket</div><div class='val green'>" + htmlEscape(lastStatus) + "</div></div>";
  page += "<div class='stat'><div class='label'>Node ID</div><div class='val'>" + htmlEscape(lastNode) + "</div></div>";
  page += "<div class='stat'><div class='label'>Counter</div><div class='val purple'>" + htmlEscape(lastCounter) + "</div></div>";
  page += "<div class='stat'><div class='label'>RSSI</div><div class='val green'>" + htmlEscape(lastRssi) + " dBm</div></div>";
  page += "<div class='stat'><div class='label'>SNR</div><div class='val yellow'>" + htmlEscape(lastSnr) + " dB</div></div>";
  page += "</div>";

  page += "<div class='metrics'>";
  page += "<div class='metric'><h2>Tegangan</h2><div class='num cyan'>" + String(lastVoltage, 3) + "<span class='unit'>V</span></div><div class='spark'></div></div>";
  page += "<div class='metric'><h2>Arus</h2><div class='num green'>" + String(lastCurrent, 2) + "<span class='unit'>mA</span></div><div class='spark'></div></div>";
  page += "<div class='metric'><h2>Daya</h2><div class='num purple'>" + String(lastPower, 2) + "<span class='unit'>mW</span></div><div class='spark'></div></div>";
  page += "</div>";

  page += "<div class='panels'>";

  page += "<div class='panel'><h3>Plaintext Hasil Dekripsi <span class='tag'>VALID</span></h3>";
  page += "<pre class='plain'>" + htmlEscape(lastPlaintext) + "</pre>";
  page += "</div>";

  page += "<div class='panel'><h3>Ciphertext HEX <span class='tag'>ENCRYPTED</span></h3>";
  page += "<pre class='hex'>ct=" + htmlEscape(lastCipherHex) + "\n\ntag=" + htmlEscape(lastTagHex) + "</pre>";
  page += "</div>";

  page += "<div class='panel'><h3>Paket LoRa Mentah <span class='tag'>RAW</span></h3>";
  page += "<pre class='raw'>" + htmlEscape(lastPacket) + "</pre>";
  page += "</div>";

  page += "</div>";

  page += "<div class='foot'>";
  page += "<div>ESP32 LoRa RX | Mode: Receive</div>";
  page += "<div>Uptime: " + String(millis() / 1000) + " detik</div>";
  page += "</div>";

  page += "</div></body></html>";

  return page;
}

void handleRoot() {
  server.send(200, "text/html", dashboardPage());
}

void processPacket(const String &packet) {
  lastPacket = packet;

  String ver = getField(packet, "ver");
  String node = getField(packet, "node");
  String ctr = getField(packet, "ctr");
  String nonceHex = getField(packet, "nonce");
  String cipherHex = getField(packet, "ct");
  String tagHex = getField(packet, "tag");

  lastNode = node;
  lastCounter = ctr;
  lastCipherHex = cipherHex;
  lastTagHex = tagHex;

  if (
    ver.length() == 0 ||
    node.length() == 0 ||
    ctr.length() == 0 ||
    nonceHex.length() == 0 ||
    cipherHex.length() == 0 ||
    tagHex.length() == 0
  ) {
    lastStatus = "Format invalid";
    Serial.println("Format paket invalid.");
    return;
  }

  uint8_t nonce[NONCE_LEN];
  size_t nonceLen = 0;

  bool nonceOk = hexToBytes(nonceHex, nonce, NONCE_LEN, nonceLen);

  if (!nonceOk || nonceLen != NONCE_LEN) {
    lastStatus = "Nonce invalid";
    Serial.println("Nonce invalid.");
    return;
  }

  String associatedData = "";
  associatedData += "ver=" + ver + ";";
  associatedData += "node=" + node + ";";
  associatedData += "ctr=" + ctr;

  String plaintext = "";

  bool ok = decryptPayload(
    associatedData,
    cipherHex,
    tagHex,
    nonce,
    plaintext
  );

  if (!ok) {
    lastStatus = "Dekripsi gagal";
    lastPlaintext = "-";

    Serial.println();
    Serial.println("Dekripsi gagal. Key, nonce, AD, ciphertext, atau tag tidak cocok.");
    return;
  }

  lastStatus = "Paket Baru Diterima";
  lastPlaintext = plaintext;

  parsePlaintextSensor(plaintext);

  Serial.println();
  Serial.println("===== RX PAKET DITERIMA =====");

  Serial.println();
  Serial.println("Raw packet:");
  Serial.println(packet);

  Serial.println();
  Serial.println("Ciphertext HEX:");
  Serial.println(cipherHex);

  Serial.println();
  Serial.println("Tag HEX:");
  Serial.println(tagHex);

  Serial.println();
  Serial.println("Plaintext hasil dekripsi:");
  Serial.println(plaintext);

  Serial.println();
  Serial.print("Dashboard: http://");
  Serial.println(WiFi.softAPIP());
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("=== RX LoRa + Xoodyak.hpp + Dashboard ===");

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
  server.begin();

  Serial.println();
  Serial.println("Dashboard WiFi aktif.");
  Serial.print("SSID     : ");
  Serial.println(AP_SSID);
  Serial.print("Password : ");
  Serial.println(AP_PASS);
  Serial.print("URL      : http://");
  Serial.println(WiFi.softAPIP());

  Serial.println();
  Serial.println("Menunggu paket LoRa...");
}

void loop() {
  server.handleClient();

  int packetSize = LoRa.parsePacket();

  if (packetSize) {
    String packet = "";

    while (LoRa.available()) {
      packet += (char)LoRa.read();
    }

    lastRssi = String(LoRa.packetRssi());
    lastSnr = String(LoRa.packetSnr(), 2);

    processPacket(packet);
  }
}