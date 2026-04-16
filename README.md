# PhoneCanvas 📱

Turn any Android phone into a wireless 128×128 display for ESP32 over BLE.

The phone app is a **dumb renderer** — it never needs updating. Only the ESP32 code changes to control what's displayed. Exactly like an OLED, but wireless on your phone.

![PhoneCanvas Demo](demo.gif)

## How it works

1. ESP32 renders graphics into a 64×64 framebuffer
2. Packs it as 4-bit pixels (2048 bytes)
3. Sends over BLE as a raw bitmap
4. Flutter app decodes and renders instantly

## Features

- 🔵 Wireless BLE display — no cables
- 📱 Phone is a dumb renderer — app never needs updating
- 🎮 Full graphics API: pixels, lines, rectangles, circles, text
- 🍩 3D spinning donut demo (real torus math + z-buffer)
- ⚡ Bitmap protocol for smooth animation

## Stack

- **ESP32**: PlatformIO + NimBLE-Arduino 2.5.0
- **Flutter**: flutter_blue_plus

## Project Structure 


phonecanvas/
├── esp32/          # ESP32 firmware (PlatformIO)
│   ├── src/main.cpp
│   └── lib/PhoneCanvas/
└── flutter/        # Android app (Flutter)
└── lib/main.dart

## Getting Started

### ESP32
1. Open `esp32/` in PlatformIO
2. Upload to ESP32

### Flutter App
1. Open `flutter/` in VS Code
2. Run `flutter pub get`
3. Run `flutter run`

## Binary Protocol

| Command | Code | Description |
|---------|------|-------------|
| PC_CLEAR | 0x10 | Clear screen |
| PC_COLOR | 0x11 | Set color (r,g,b) |
| PC_PIXEL | 0x12 | Draw pixel |
| PC_LINE | 0x13 | Draw line |
| PC_RECT | 0x14 | Draw rectangle |
| PC_FILLRECT | 0x15 | Filled rectangle |
| PC_CIRCLE | 0x16 | Draw circle |
| PC_FILLCIRCLE | 0x17 | Filled circle |
| PC_TEXT | 0x18 | Draw text |
| PC_SHOW | 0x1B | Flush frame |
| PC_FRAME | 0x20 | Raw bitmap frame |

## Built by

Gaurav Sharma — [@gauravshar64966](https://x.com/gauravshar64966)
