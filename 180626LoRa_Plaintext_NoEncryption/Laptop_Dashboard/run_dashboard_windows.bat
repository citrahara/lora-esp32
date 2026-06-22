@echo off
setlocal
cd /d "%~dp0"
where python >nul 2>nul
if errorlevel 1 (
  echo Python tidak ditemukan. Install Python 3 dulu.
  pause
  exit /b 1
)
python -m pip install -r requirements.txt
set /p COMPORT=Masukkan COM RX, contoh COM10: 
python dashboard_serial_local.py --port %COMPORT% --sync-time
pause
