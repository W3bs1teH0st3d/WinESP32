@echo off
REM Build script for Win32 OS

echo ========================================
echo   Win32 OS - Build Tool
echo ========================================
echo.

REM Activate ESP-IDF environment
call C:\Espressif\frameworks\esp-idf-v5.4.3\export.bat

echo.
echo Building project...
echo.

idf.py build

if %ERRORLEVEL% EQU 0 (
    echo.
    echo ========================================
    echo   Build successful!
    echo ========================================
    echo.
    echo To flash, run:
    echo   flash_and_monitor.bat
    echo.
) else (
    echo.
    echo ========================================
    echo   Build failed!
    echo ========================================
    echo.
)

pause
