@echo off
cd /d "%~dp0"
echo Dashboard LoRa Xoodyak - Lokal + LAN/Web
echo.
echo Alur: TX - LoRa - RX - USB Serial - laptop penerima - browser.
echo Akses di laptop penerima: http://127.0.0.1:8000
echo Akses dari HP/laptop lain satu WiFi: http://IP-LAPTOP-PENERIMA:8000
echo.
echo PENTING: masukkan port COM milik RX, bukan TX.
echo Tutup Arduino Serial Monitor / Serial Plotter sebelum lanjut.
echo.
set /p PORT=Masukkan port COM RX, contoh COM16: 
python -m pip show pyserial >nul 2>nul
if errorlevel 1 (
  echo pyserial belum ada. Menginstall pyserial...
  python -m pip install pyserial
)
python dashboard_serial_local.py --host 0.0.0.0 --port %PORT% --baud 115200
pause
