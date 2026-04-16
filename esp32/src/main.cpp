#include <Arduino.h>
#include <NimBLEDevice.h>
#include <math.h>

#define NUS_SERVICE_UUID  "6E400001-B5A4-11E6-A567-006BEB35965A"
#define NUS_TX_UUID       "6E400003-B5A4-11E6-A567-006BEB35965A"
#define NUS_RX_UUID       "6E400002-B5A4-11E6-A567-006BEB35965A"

#define PC_CLEAR    0x10
#define PC_COLOR    0x11
#define PC_PIXEL    0x12
#define PC_LINE     0x13
#define PC_RECT     0x14
#define PC_FRECT    0x15
#define PC_CIRCLE   0x16
#define PC_FCIRCLE  0x17
#define PC_TEXT     0x18
#define PC_SHOW     0x1B

NimBLECharacteristic* gTxChar = nullptr;
NimBLECharacteristic* gRxChar = nullptr;
bool gConnected  = false;
volatile bool gReady = true;

class RxCB : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* c, NimBLEConnInfo&) override {
    if (c->getValue().data()[0] == 0x01) gReady = true;
  }
};

class SrvCB : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer*, NimBLEConnInfo&) override { gConnected = true; gReady = true; }
  void onDisconnect(NimBLEServer*, NimBLEConnInfo&, int) override {
    gConnected = false;
    NimBLEDevice::startAdvertising();
  }
};

// ── Command buffer ────────────────────────────────────────────────────────────
#define CMDBUF_SIZE 490
static uint8_t cmdBuf[CMDBUF_SIZE];
static uint8_t frameBuf[2 + CMDBUF_SIZE];
static size_t  cmdLen = 0;

void cmdFlush() {
  if (!gConnected || cmdLen == 0) { cmdLen = 0; return; }
  frameBuf[0] = (cmdLen >> 8) & 0xFF;
  frameBuf[1] = cmdLen & 0xFF;
  memcpy(frameBuf + 2, cmdBuf, cmdLen);
  size_t total = 2 + cmdLen;
  size_t mtu = NimBLEDevice::getMTU() - 3;
  if (mtu < 20) mtu = 20;
  size_t offset = 0;
  while (offset < total) {
    size_t chunk = min(mtu, total - offset);
    gTxChar->setValue(frameBuf + offset, chunk);
    gTxChar->notify();
    offset += chunk;
    if (offset < total) delay(40);
  }
  cmdLen = 0;
}

void cmdPush(const uint8_t* data, size_t len) {
  if (cmdLen + len > CMDBUF_SIZE) cmdFlush();
  memcpy(cmdBuf + cmdLen, data, len);
  cmdLen += len;
}

// ── Drawing API ───────────────────────────────────────────────────────────────
void gClear()   { uint8_t c = PC_CLEAR; cmdPush(&c, 1); }
void gShow()    { uint8_t c = PC_SHOW;  cmdPush(&c, 1); cmdFlush(); }

void gColor(uint8_t r, uint8_t g, uint8_t b) {
  uint8_t c[4] = {PC_COLOR, r, g, b}; cmdPush(c, 4);
}
void gFillCircle(uint8_t x, uint8_t y, uint8_t r) {
  uint8_t c[4] = {PC_FCIRCLE, x, y, r}; cmdPush(c, 4);
}
void gText(uint8_t x, uint8_t y, const char* text) {
  uint8_t len = strlen(text);
  uint8_t h[4] = {PC_TEXT, x, y, len};
  cmdPush(h, 4);
  cmdPush((uint8_t*)text, len);
}

// ── Donut — bitmap frame protocol ────────────────────────────────────────────
#define PC_FRAME    0x20
#define FB_SZ       64
#define FB_PACKED   (FB_SZ * FB_SZ / 2)  // 2048 bytes

float rotA = 0, rotB = 0;
static uint8_t fb[FB_SZ][FB_SZ];
static float   zb[FB_SZ][FB_SZ];
static uint8_t packed[4 + FB_PACKED];     // header + pixel data

void sendFrame() {
  // Clear buffers
  memset(fb, 0, sizeof(fb));
  memset(zb, 0, sizeof(zb));

  float cosA = cos(rotA), sinA = sin(rotA);
  float cosB = cos(rotB), sinB = sin(rotB);

  // Render donut into framebuffer
  for (float j = 0; j < 6.28f; j += 0.08f) {
    float cosj = cos(j), sinj = sin(j);
    for (float i = 0; i < 6.28f; i += 0.025f) {
      float cosi = cos(i), sini = sin(i);
      float h = cosj + 2;
      float D = 1.0f / (sini * h * sinA + sinj * cosA + 5);
      float t = sini * h * cosA - sinj * sinA;
      int x = (int)(FB_SZ/2 + FB_SZ * 0.4f * D * (cosi * h * cosB - t * sinB));
      int y = (int)(FB_SZ/2 - FB_SZ * 0.4f * D * (cosi * h * sinB + t * cosB));
      if (x < 0 || x >= FB_SZ || y < 0 || y >= FB_SZ) continue;
      if (D > zb[y][x]) {
        zb[y][x] = D;
        float L = cosj * cosB * sinA - cosA * sinj * cosB
                + cosi * cosj * sinB - sinj * sinA * sini * sinB
                + cosi * sinj * cosA;
        fb[y][x] = L > 0 ? (uint8_t)(L * 7 + 1) : 0;
      }
    }
    yield();
  }

  // Pack 4-bit: two pixels per byte
  packed[0] = PC_FRAME;
  packed[1] = (FB_PACKED >> 16) & 0xFF;
  packed[2] = (FB_PACKED >> 8)  & 0xFF;
  packed[3] = FB_PACKED & 0xFF;
  for (int idx = 0; idx < FB_SZ * FB_SZ; idx += 2) {
    uint8_t p1 = fb[idx / FB_SZ][idx % FB_SZ];
    uint8_t p2 = fb[(idx+1) / FB_SZ][(idx+1) % FB_SZ];
    packed[4 + idx/2] = (p1 << 4) | (p2 & 0x0F);
  }

  // Send raw — bypass cmdFlush, direct BLE notifications
  if (!gConnected) return;
  size_t total = 4 + FB_PACKED;  // 2052 bytes
  size_t mtu = NimBLEDevice::getMTU() - 3;
  if (mtu < 20) mtu = 20;
  size_t chunkSz = min(mtu, (size_t)500);
  size_t offset = 0;
  while (offset < total) {
    size_t n = min(chunkSz, total - offset);
    gTxChar->setValue(packed + offset, n);
    gTxChar->notify();
    offset += n;
    if (offset < total) delay(10);
  }
}

void setup() {
  NimBLEDevice::init("PhoneCanvas");
  NimBLEDevice::setMTU(512);
  NimBLEServer* server = NimBLEDevice::createServer();
  server->setCallbacks(new SrvCB());
  NimBLEService* svc = server->createService(NUS_SERVICE_UUID);
  gTxChar = svc->createCharacteristic(NUS_TX_UUID, NIMBLE_PROPERTY::NOTIFY);
  gRxChar = svc->createCharacteristic(NUS_RX_UUID, NIMBLE_PROPERTY::WRITE);
  gRxChar->setCallbacks(new RxCB());
  server->start();
  NimBLEDevice::startAdvertising();
}

void loop() {
  if (!gConnected) { delay(200); return; }
  sendFrame();
  rotA += 0.05f;
  rotB += 0.02f;
}