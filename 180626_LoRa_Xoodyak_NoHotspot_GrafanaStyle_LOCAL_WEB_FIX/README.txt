LoRa Xoodyak No Hotspot - Dashboard Serial Lokal + LAN/Web
===========================================================

Alur sistem:
TX ESP32 + INA219 -> LoRa -> RX ESP32 -> USB Serial -> Laptop Penerima -> Browser

Makna asli tetap dipertahankan:
- ESP32 RX tidak membuat WiFi/hotspot dan tidak menjalankan WebServer.
- Dashboard web tetap dijalankan di laptop penerima.
- Data dashboard tetap berasal dari USB Serial RX.
- TX mengirim paket setiap 4 detik.
- Timestamp utama dashboard memakai jam laptop GMT+7/WIB.

Perbaikan versi ini:
1. Server dashboard default listen di 0.0.0.0.
   Artinya dashboard bisa dibuka dari laptop penerima dan perangkat lain pada jaringan yang sama.
2. Akses lokal laptop penerima:
   http://127.0.0.1:8000
3. Akses dari HP/laptop lain satu WiFi/LAN:
   http://IP-LAPTOP-PENERIMA:8000
   Contoh: http://192.168.1.25:8000
4. Tabel dashboard sekarang menampilkan semua paket RX yang terbaca, termasuk FORMAT_INVALID/NONCE_INVALID/DEKRIPSI_GAGAL.
   Grafik tetap hanya memakai data VALID agar grafik tidak rusak oleh nilai kosong.
5. Endpoint API dibuat lebih aman untuk akses browser/LAN:
   /api/data
   /api/health

Penting, karena 127.0.0.1 sering disalahpahami:
- 127.0.0.1 dari laptop penerima = laptop penerima.
- 127.0.0.1 dari HP/laptop lain = perangkat itu sendiri, bukan laptop penerima.
  Jadi untuk akses dari perangkat lain, gunakan IP laptop penerima.

Cara pakai Windows:
1. Upload sketch TX_INA_Xoodyak_GMT7_Local ke board transmitter.
2. Upload sketch RX_Gateway_Xoodyak_SerialOnly ke board receiver.
3. Cabut/restart kedua board setelah upload.
4. Tutup Arduino Serial Monitor dan Serial Plotter.
5. Masuk folder Laptop_Dashboard.
6. Jalankan run_dashboard_windows.bat.
7. Masukkan COM milik RX, bukan COM TX.
8. Buka dashboard:
   - Di laptop penerima: http://127.0.0.1:8000
   - Dari perangkat lain satu jaringan: http://IP-LAPTOP-PENERIMA:8000

Cara manual:
python -m pip install pyserial
python dashboard_serial_local.py --host 0.0.0.0 --port COM16 --baud 115200

Opsional, hanya kalau RX sudah memakai sketch yang mendukung TIME_SYNC:
python dashboard_serial_local.py --host 0.0.0.0 --port COM16 --baud 115200 --sync-time

Indikator benar:
- Terminal dashboard menampilkan: Serial tersambung: COMxx @ 115200
- Terminal dashboard menampilkan baris RX setiap sekitar 4 detik.
- Web dashboard menampilkan Serial: COMxx @ 115200.
- Counter bertambah jika paket valid diterima.
- Jika paket invalid diterima, tabel tetap bertambah dan status terlihat di kolom Status.

Jika dashboard lokal bisa dibuka tetapi perangkat lain tidak bisa:
- Pastikan perangkat lain berada di jaringan WiFi/LAN yang sama.
- Gunakan IP laptop penerima, bukan 127.0.0.1.
- Izinkan Python/port 8000 di Windows Firewall.
- Coba matikan VPN sementara jika VPN memblokir akses LAN.

Jika dashboard kosong:
- Cek COM yang dipilih adalah COM RX.
- Tutup Arduino Serial Monitor/Serial Plotter.
- Pastikan baud 115200.
- Cek TX dan RX memakai frekuensi, Spreading Factor, Bandwidth, Coding Rate, dan key Xoodyak yang sama.
- Cek LED RX berkedip saat paket LoRa masuk.
