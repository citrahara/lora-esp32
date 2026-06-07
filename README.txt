LoRa Xoodyak No-Hotspot Local Laptop Dashboard - Grafana Style
=================================================================

Versi ini mengembalikan tampilan dashboard seperti yang lama/kanan:
- 3 grafik sejajar: Tegangan, Arus, Daya.
- Summary tiap penerimaan paket.
- Tabel histori paket.
- Panel plaintext dan raw serial.
- Warna light theme seperti dashboard sebelumnya.

Yang penting:
- ESP32 RX TIDAK membuat hotspot.
- ESP32 RX TIDAK connect WiFi.
- ESP32 RX TIDAK menjalankan WebServer.
- Dashboard web berjalan DI LAPTOP penerima lewat Python.
- Akses dashboard: http://127.0.0.1:8000

Alur data:
TX -> LoRa -> RX -> USB Serial -> Laptop Dashboard -> Browser localhost

Struktur folder:
1. TX_INA_Xoodyak_GMT7_Local
   Upload ke board TX. Ini membaca INA219, encrypt Xoodyak, kirim LoRa.

2. RX_Gateway_Xoodyak_SerialOnly
   Upload ke board RX. Ini menerima LoRa dan print 1 paket = 1 baris di Serial.

3. Laptop_Dashboard
   Jalankan di laptop yang tersambung USB ke board RX.

Cara pakai Windows:
1. Upload TX_INA_Xoodyak_GMT7_Local ke ESP32-S3 TX.
2. Upload RX_Gateway_Xoodyak_SerialOnly ke ESP32-S3 RX.
3. Colok RX ke laptop.
4. Tutup Serial Monitor Arduino IDE kalau sedang membuka port RX.
   COM port tidak bisa dipakai Arduino Serial Monitor dan Python bersamaan.
5. Buka folder Laptop_Dashboard.
6. Klik dua kali run_dashboard_windows.bat.
7. Masukkan COM RX, contoh COM16.
8. Buka browser di laptop: http://127.0.0.1:8000

Kalau dashboard kosong:
- Pastikan port COM yang dimasukkan adalah port RX, bukan TX.
- Pastikan Serial Monitor Arduino IDE sudah ditutup.
- Pastikan RX Serial Monitor sebelumnya menampilkan baris yang diawali "RX |".
- Pastikan baudrate RX 115200.

Catatan timestamp GMT+7:
- TX mengirim field rt=yyyy-mm-dd hh:mm:ss di plaintext terenkripsi.
- RX menampilkan timestamp itu setelah decrypt sukses.
- Dashboard laptop menampilkan timestamp yang sama.
- Sumber waktu TX saat ini adalah waktu compile/upload dari laptop, bukan NTP.
- Kalau butuh waktu akurat meski board dimatikan/direset lama, pakai RTC DS3231.
