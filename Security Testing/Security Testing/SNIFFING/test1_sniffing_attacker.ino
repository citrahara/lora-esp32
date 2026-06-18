#include <SPI.h>
#include <LoRa.h>

#define LORA_SCK   12
#define LORA_MISO  13
#define LORA_MOSI  11
#define LORA_SS    10
#define LORA_RST   5
#define LORA_DIO0  4

#define LORA_FREQ 435E6

void setup() {
  Serial.begin(115200);
  delay(1000);

  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_SS);
  LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);

  if (!LoRa.begin(LORA_FREQ)) {
    Serial.println("LoRa gagal start");
    while (1);
  }

  LoRa.setSpreadingFactor(7);
  LoRa.setSignalBandwidth(125E3);
  LoRa.setCodingRate4(5);
  LoRa.enableCrc();

  Serial.println("Sniffer aktif...");
}

void loop() {
  int packetSize = LoRa.parsePacket();

  if (packetSize) {
    String data = "";

    while (LoRa.available()) {
      data += (char)LoRa.read();
    }

    Serial.println("=== Paket Tertangkap ===");
    Serial.print("RSSI: ");
    Serial.println(LoRa.packetRssi());

    Serial.print("SNR : ");
    Serial.println(LoRa.packetSnr());

    Serial.print("DATA: ");
    Serial.println(data);
  }
}