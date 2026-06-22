LoRa Plaintext No Encryption
============================

Isi paket:
1. TX_INA_Plaintext_GMT7_Local
   - ESP32-S3 transmitter.
   - Membaca INA219 dan mengirim payload plaintext lewat LoRa.

2. RX_Gateway_Plaintext_SerialOnly
   - ESP32-S3 receiver.
   - Menerima payload plaintext LoRa dan mencetak tabel Serial.
   - Tidak memakai WiFi, hotspot, WebServer, Xoodyak, nonce, ciphertext, atau tag.

3. Laptop_Dashboard
   - Dashboard lokal/LAN berbasis Python.
   - Membaca USB Serial dari board RX.
   - Menampilkan grafik tegangan, arus, daya, tabel paket, plaintext, dan raw packet.

Format payload LoRa:
ver=1;node=K1;ctr=1;rt=2026-06-12 08:00:00;ts=123456;v=3.300;i=120.00;p=396.00

Catatan penting:
- TX dan RX diatur memakai LORA_FREQ 433E6.
- Jika modul Anda memakai 439 MHz, ubah LORA_FREQ pada TX dan RX menjadi nilai yang sama.
- Kode ini sengaja tanpa enkripsi. Data bisa dibaca siapa pun yang menerima sinyal LoRa pada frekuensi dan konfigurasi radio yang sama.
- LoRa CRC diaktifkan untuk membantu mendeteksi error transmisi, tetapi CRC bukan keamanan.

Cara pakai:
1. Upload TX_INA_Plaintext_GMT7_Local.ino ke board transmitter.
2. Upload RX_Gateway_Plaintext_SerialOnly.ino ke board receiver.
3. Hubungkan RX ke laptop via USB.
4. Tutup Serial Monitor Arduino agar port COM tidak bentrok.
5. Jalankan dashboard:
   Windows: buka Laptop_Dashboard/run_dashboard_windows.bat
   Manual : python dashboard_serial_local.py --port COM10 --sync-time
6. Buka browser:
   http://127.0.0.1:8000

Library Arduino yang dibutuhkan:
- LoRa by Sandeep Mistry
- Adafruit INA219
