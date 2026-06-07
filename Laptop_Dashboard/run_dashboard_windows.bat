@echo off
cd /d "%~dp0"
echo Dashboard lokal LoRa Xoodyak - Grafana Style
echo Tidak memakai hotspot ESP32. Akses: http://127.0.0.1:8000
echo.
set /p PORT=Masukkan port COM RX, contoh COM16: 
python -m pip show pyserial >nul 2>nul
if errorlevel 1 (
  echo pyserial belum ada. Menginstall pyserial...
  python -m pip install pyserial
)
python dashboard_serial_local.py --port %PORT% --baud 115200
pause
