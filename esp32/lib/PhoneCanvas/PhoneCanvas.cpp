#include "PhoneCanvas.h"

#define NUS_SVC "6E400001-B5A4-11E6-A567-006BEB35965A"
#define NUS_TX  "6E400003-B5A4-11E6-A567-006BEB35965A"
#define NUS_RX  "6E400002-B5A4-11E6-A567-006BEB35965A"

// ── begin ─────────────────────────────────────────────────────────────────────
void PhoneCanvasClass::begin(const char* deviceName) {
  NimBLEDevice::init(deviceName);
  NimBLEDevice::setMTU(512);
  _server = NimBLEDevice::createServer();
  _server->setCallbacks(new CBs(this));
  NimBLEService* svc = _server->createService(NUS_SVC);
  _txChar = svc->createCharacteristic(NUS_TX, NIMBLE_PROPERTY::NOTIFY);
  svc->createCharacteristic(NUS_RX, NIMBLE_PROPERTY::WRITE);
  _server->start();
  NimBLEDevice::startAdvertising();
}

bool PhoneCanvasClass::connected() { return _connected; }

// ── _push — add bytes to command buffer ──────────────────────────────────────
void PhoneCanvasClass::_push(const uint8_t* data, size_t len) {
  if (_len + len > BUFSIZE) _flush(); // auto-flush if buffer full
  memcpy(_buf + _len, data, len);
  _len += len;
}

// ── _flush — send buffer over BLE with 2-byte length prefix ──────────────────
void PhoneCanvasClass::_flush() {
  if (!_connected || _len == 0) { _len = 0; return; }
  // Build framed packet
  static uint8_t frame[2 + BUFSIZE];
  frame[0] = (_len >> 8) & 0xFF;
  frame[1] = _len & 0xFF;
  memcpy(frame + 2, _buf, _len);
  size_t total = 2 + _len;
  size_t mtu = NimBLEDevice::getMTU() - 3;
  if (mtu < 20) mtu = 20;
  size_t offset = 0;
  while (offset < total) {
    size_t chunk = min(mtu, total - offset);
    _txChar->setValue(frame + offset, chunk);
    _txChar->notify();
    offset += chunk;
    if (offset < total) delay(5);
  }
  _len = 0;
}

// ── Drawing commands ──────────────────────────────────────────────────────────
void PhoneCanvasClass::clear() {
  uint8_t c = PC_CLEAR;
  _push(&c, 1);
}

void PhoneCanvasClass::setColor(uint8_t r, uint8_t g, uint8_t b) {
  uint8_t c[] = { PC_COLOR, r, g, b };
  _push(c, 4);
}

void PhoneCanvasClass::drawPixel(uint8_t x, uint8_t y) {
  uint8_t c[] = { PC_PIXEL, x, y };
  _push(c, 3);
}

void PhoneCanvasClass::drawLine(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2) {
  uint8_t c[] = { PC_LINE, x1, y1, x2, y2 };
  _push(c, 5);
}

void PhoneCanvasClass::drawRect(uint8_t x, uint8_t y, uint8_t w, uint8_t h) {
  uint8_t c[] = { PC_RECT, x, y, (uint8_t)(x+w), (uint8_t)(y+h) };
  _push(c, 5);
}

void PhoneCanvasClass::fillRect(uint8_t x, uint8_t y, uint8_t w, uint8_t h) {
  uint8_t c[] = { PC_FRECT, x, y, (uint8_t)(x+w), (uint8_t)(y+h) };
  _push(c, 5);
}

void PhoneCanvasClass::drawCircle(uint8_t x, uint8_t y, uint8_t r) {
  uint8_t c[] = { PC_CIRCLE, x, y, r };
  _push(c, 4);
}

void PhoneCanvasClass::fillCircle(uint8_t x, uint8_t y, uint8_t r) {
  uint8_t c[] = { PC_FCIRCLE, x, y, r };
  _push(c, 4);
}

void PhoneCanvasClass::drawText(uint8_t x, uint8_t y, const char* text) {
  uint8_t len = strlen(text);
  uint8_t header[] = { PC_TEXT, x, y, len };
  _push(header, 4);
  _push((uint8_t*)text, len);
}

void PhoneCanvasClass::show() {
  uint8_t c = PC_SHOW;
  _push(&c, 1);
  _flush(); // send everything
}

// ── Global instance ───────────────────────────────────────────────────────────
PhoneCanvasClass Canvas;