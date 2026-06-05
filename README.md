# LoRa Xoodyak Electrical Telemetry Dashboard

Project ini memisahkan receiver LoRa dan frontend dashboard.

## Struktur

```text
lora_xoodyak_dashboard_project/
├── TX_INA_Xoodyak_Updated/
│   ├── TX_INA_Xoodyak_Updated.ino
│   └── *.hpp Xoodyak
├── RX_Gateway_Xoodyak_Dashboard/
│   ├── RX_Gateway_Xoodyak_Dashboard.ino
│   ├── DashboardUI.h
│   └── *.hpp Xoodyak
└── dashboard_preview.html
```

## Revisi dashboard terbaru

Dashboard sekarang mengikuti layout meteo/Grafana-style:

- tidak memakai sidebar;
- tiga grafik dalam satu baris horizontal: Tegangan, Arus, dan Daya;
- sumbu X berisi timestamp GMT+7/WIB;
- sumbu Y berisi nilai sensor sesuai unit: V, mA, dan mW;
- legend di bawah grafik menampilkan node dan nilai terakhir;
- summary tiap penerimaan paket ada di bawah grafik;
- plaintext dan raw packet tetap tersedia untuk debugging.

## Cara upload

1. Upload `TX_INA_Xoodyak_Updated.ino` ke node transmitter.
2. Upload `RX_Gateway_Xoodyak_Dashboard.ino` ke node receiver.
3. Hubungkan laptop/HP ke WiFi receiver:

```text
SSID     : LoRa-RX-Dashboard
Password : 12345678
```

4. Buka:

```text
http://192.168.4.1
```

## Preview frontend tanpa hardware

Buka `dashboard_preview.html` langsung di browser. Jika dibuka sebagai file lokal, dashboard otomatis memakai data simulasi.

## Catatan timestamp

ESP32 mode AP tidak punya waktu real tanpa RTC/NTP. Timestamp GMT+7 pada grafik dihitung dari:

```text
waktu browser saat fetch API - selisih uptime ESP32 terhadap rx_ms paket
```

Untuk timestamp absolut dari perangkat, tambahkan RTC DS3231 atau koneksi NTP.
