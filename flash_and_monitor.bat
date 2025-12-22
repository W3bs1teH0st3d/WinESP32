@echo off
REM Flash and Monitor script for Win32 OS
REM Usage: flash_and_monitor.bat [COM_PORT]

set COM_PORT=%1
if "%COM_PORT%"=="" set COM_PORT=COM3

echo ========================================
echo   Win32 OS - Flash and Monitor
echo ========================================
echo.
echo Target: ESP32-P4
echo Port: %COM_PORT%
echo.

REM Activate ESP-IDF environment
call C:\Espressif\frameworks\esp-idf-v5.4.3\export.bat

echo.
echo Flashing firmware and starting monitor...
echo.
echo Press Ctrl+] to exit monitor
echo.

idf.py -p %COM_PORT% flash monitor
