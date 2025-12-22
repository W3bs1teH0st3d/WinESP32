# WinESP32 - Windows Vista Style OS for ESP32-P4

Retro Windows Vista-inspired operating system for ESP32-P4 with 480x800 touchscreen.

## Features

- Windows Vista/7 style desktop with icons and taskbar
- Start menu, system tray (WiFi, BT, battery)
- Multiple wallpapers (XP, Vista, 7, 8, 10, 11)
- Lock screen with AOD mode
- Recovery mode (triple-press BOOT)

### Apps
Calculator, Clock, Weather, Notepad, Paint, Camera, Photo Viewer, File Manager, Voice Recorder, System Monitor, Console, JavaScript IDE, Settings

### Games
Snake, Flappy Bird, Tetris, 2048, Minesweeper, Tic-Tac-Toe, Memory

## Hardware

- **Board**: JC4880P443C (ESP32-P4)
- **Display**: 480x800 ST7701S MIPI-DSI + GT911 touch
- **Camera**: OV02C10 MIPI-CSI (optional)
- **WiFi/BT**: ESP32-C6 via ESP-Hosted
- **Storage**: 16MB Flash, 32MB PSRAM

## Build

Requires ESP-IDF v5.4.3+

```batch
build.bat
flash.bat
```

Or manually:
```
idf.py set-target esp32p4
idf.py build
idf.py -p COM3 flash
```

## Structure

```
RWinESP32/
├── main/           # Source code
│   ├── ui/         # UI components
│   ├── hardware/   # HAL drivers
├── components/     # LVGL, drivers, duktape
├── assets/converted/  # Compiled images
├── utils/          # Asset converter + raw icons
```

## Utils

Convert new icons:
```
cd utils
python convert_assets.py
```

## License

MIT
