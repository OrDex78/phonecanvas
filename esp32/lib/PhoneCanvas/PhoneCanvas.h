#pragma once
#include <Arduino.h>
#include <NimBLEDevice.h>

// ── Command bytes ──────────────────────────────────────────────────────────────
#define PC_CLEAR    0x10  // no args
#define PC_COLOR    0x11  // r g b
#define PC_PIXEL    0x12  // x y
#define PC_LINE     0x13  // x1 y1 x2 y2
#define PC_RECT     0x14  // x1 y1 x2 y2
#define PC_FRECT    0x15  // x1 y1 x2 y2
#define PC_CIRCLE   0x16  // x y r
#define PC_FCIRCLE  0x17  // x y r
#define PC_TEXT     0x18  // x y len str...
#define PC_SHOW     0x1B  // no args — flush frame to screen

// Virtual screen: 128x128 logical pixels
// Flutter scales to fill phone screen maintaining aspect ratio

class PhoneCanvasClass {
public:
  void begin(const char* deviceName = "PhoneCanvas");
  bool connected();

  // ── Drawing API (mirrors u8g2 / Adafruit GFX) ──────────────────────────────
  void clear();
  void setColor(uint8_t r, uint8_t g, uint8_t b);
  void drawPixel(uint8_t x, uint8_t y);
  void drawLine(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2);
  void drawRect(uint8_t x, uint8_t y, uint8_t w, uint8_t h);
  void fillRect(uint8_t x, uint8_t y, uint8_t w, uint8_t h);
  void drawCircle(uint8_t x, uint8_t y, uint8_t r);
  void fillCircle(uint8_t x, uint8_t y, uint8_t r);
  void drawText(uint8_t x, uint8_t y, const char* text);
  void show();  // flush all buffered commands to phone — call once per frame

private:
  NimBLEServer*         _server   = nullptr;
  NimBLECharacteristic* _txChar   = nullptr;
  bool                  _connected = false;

  // Command buffer — batches all commands between clear() and show()
  static const size_t BUFSIZE = 490;
  uint8_t _buf[BUFSIZE];
  size_t  _len = 0;

  void _push(const uint8_t* data, size_t len);
  void _flush();

  // BLE callbacks
  class CBs : public NimBLEServerCallbacks {
    PhoneCanvasClass* _parent;
  public:
    CBs(PhoneCanvasClass* p) : _parent(p) {}
    void onConnect(NimBLEServer*, NimBLEConnInfo&) override    { _parent->_connected = true; }
    void onDisconnect(NimBLEServer*, NimBLEConnInfo&, int) override {
      _parent->_connected = false;
      NimBLEDevice::startAdvertising();
    }
  };
};

extern PhoneCanvasClass Canvas;