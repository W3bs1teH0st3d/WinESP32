@echo off
REM Monitor script for Win32 OS
REM Usage: monitor.bat [COM_PORT]

set COM_PORT=%1
if "%COM_PORT%"=="" set COM_PORT=COM3

echo ========================================
echo   Win32 OS - Serial Monitor
echo ========================================
echo.
echo Port: %COM_PORT%
echo Baud: 115200
echo.
echo Press Ctrl+] to exit
echo.

REM Activate ESP-IDF environment
call C:\Espressif\frameworks\esp-idf-v5.4.3\export.bat

idf.py -p %COM_PORT% monitor
