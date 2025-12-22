@echo off
REM Flash script for Win32 OS
REM Usage: flash.bat [COM_PORT]

set COM_PORT=%1
if "%COM_PORT%"=="" set COM_PORT=COM3

echo ========================================
echo   Win32 OS - Flash Tool
echo ========================================
echo.
echo Target: ESP32-P4
echo Port: %COM_PORT%
echo.

REM Activate ESP-IDF environment
call C:\Espressif\frameworks\esp-idf-v5.4.3\export.bat

echo.
echo Flashing firmware...
echo.

idf.py -p %COM_PORT% flash

echo.
echo ========================================
echo   Flash complete!
echo ========================================
echo.
echo To monitor output, run:
echo   monitor.bat %COM_PORT%
echo.
pause
