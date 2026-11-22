/*
 * Visor de guidão: próxima curva do Sygic na ST7789 240x240.
 *
 * O iPhone (app Sygic, modo BLE HUD) manda a próxima manobra.
 * A tela desenha o trecho até essa curva. O Sygic não envia o resto da rota.
 * Pinos iguais ao bringup_tft que já acende esta tela.
 *
 *   TFT VCC  -> 5 V
 *   TFT GND  -> GND
 *   TFT SCL  -> GPIO4
 *   TFT SDA  -> GPIO6
 *   TFT CS   -> GPIO7
 *   TFT DC   -> GPIO10
 *   TFT RES  -> GPIO3
 *   TFT BLK  -> GPIO5
 *
 * Placa: ESP32C3 Dev Module
 * USB CDC On Boot: Enabled
 * Monitor: 115200
 * Sem o iPhone, digite "demo" no serial para girar as setas.
 */

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

#if !defined(ARDUINO_ARCH_ESP32)
#error Selecione a placa ESP32C3 Dev Module em Ferramentas, Placa, esp32. BLEDevice.h nao existe no Uno nem no ESP8266.
#endif
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLE2902.h>
#include <math.h>

#include "hud_proto.h"

static const int TFT_SCLK = 4;
static const int TFT_MOSI = 6;
static const int TFT_CS = 7;
static const int TFT_DC = 10;
static const int TFT_RST = 3;
static const int TFT_BL = 5;

static const int TFT_OFFSET_X = 0;
static const int TFT_OFFSET_Y = 0;
static const int TFT_MEMORY_H = 240;
static const bool TFT_INVERT = true;
static const uint8_t TFT_ROTATION = 0;
static const uint32_t TFT_SPI_HZ = 27000000;
static const uint8_t TFT_BRIGHTNESS = 180;

static const int kRiderX = 120;
static const int kRiderY = 150;
static const uint16_t COL_ROAD = 0x4A69;
static const uint16_t COL_ROUTE = 0xFD20;

class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ST7789 _panel;
  lgfx::Bus_SPI _bus;
  lgfx::Light_PWM _light;

 public:
  LGFX() {
    {
      auto cfg = _bus.config();
      cfg.spi_host = SPI2_HOST;
      cfg.spi_mode = 0;
      cfg.freq_write = TFT_SPI_HZ;
      cfg.freq_read = 16000000;
      cfg.spi_3wire = true;
      cfg.use_lock = true;
      cfg.pin_sclk = TFT_SCLK;
      cfg.pin_mosi = TFT_MOSI;
      cfg.pin_miso = -1;
      cfg.pin_dc = TFT_DC;
      _bus.config(cfg);
      _panel.setBus(&_bus);
    }
    {
      auto cfg = _panel.config();
      cfg.pin_cs = TFT_CS;
      cfg.pin_rst = TFT_RST;
      cfg.pin_busy = -1;
      cfg.memory_width = 240;
      cfg.memory_height = TFT_MEMORY_H;
      cfg.panel_width = 240;
      cfg.panel_height = 240;
      cfg.offset_x = TFT_OFFSET_X;
      cfg.offset_y = TFT_OFFSET_Y;
      cfg.offset_rotation = 0;
      cfg.dummy_read_pixel = 8;
      cfg.dummy_read_bits = 1;
      cfg.readable = false;
      cfg.invert = TFT_INVERT;
      cfg.rgb_order = false;
      cfg.dlen_16bit = false;
      cfg.bus_shared = false;
      _panel.config(cfg);
    }
    {
      auto cfg = _light.config();
      cfg.pin_bl = TFT_BL;
      cfg.invert = false;
      cfg.freq = 12000;
      cfg.pwm_channel = 0;
      _light.config(cfg);
      _panel.setLight(&_light);
    }
    setPanel(&_panel);
  }
};

static LGFX lcd;

static portMUX_TYPE gMux = portMUX_INITIALIZER_UNLOCKED;
static uint8_t gPacket[HUD_PACKET_MAX];
static size_t gPacketLen = 0;
static volatile bool gPacketUpdated = false;
static volatile bool gConnected = false;
static volatile uint32_t gLastActivityMs = 0;

static BLECharacteristic* gIndicate = nullptr;

static bool gDemo = false;
static uint8_t gDemoIndex = 0;
static uint32_t gDemoMs = 0;

enum ScreenKind : uint8_t { SCREEN_NONE, SCREEN_WAITING, SCREEN_NOROUTE, SCREEN_NAV };
static ScreenKind gScreen = SCREEN_NONE;

enum ArrowShape : uint8_t {
  SHAPE_STRAIGHT,
  SHAPE_BEAR,
  SHAPE_UTURN_LEFT,
  SHAPE_UTURN_RIGHT,
  SHAPE_ROUNDABOUT,
  SHAPE_ARRIVE,
  SHAPE_PIN,
  SHAPE_OFF,
  SHAPE_ALERT,
};

struct ArrowStyle {
  ArrowShape shape;
  int16_t angle;
};

struct Pt {
  int16_t x;
  int16_t y;
};

class HudServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* server) override {
    (void)server;
    gConnected = true;
    gLastActivityMs = millis();
    Serial.println("BLE conectado");
  }

  void onDisconnect(BLEServer* server) override {
    (void)server;
    gConnected = false;
    Serial.println("BLE desconectado");
    BLEDevice::startAdvertising();
  }
};

class HudWriteCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* characteristic) override {
    String value = characteristic->getValue();
    size_t n = value.length();
    if (n > HUD_PACKET_MAX) {
      n = HUD_PACKET_MAX;
    }
    portENTER_CRITICAL(&gMux);
    if (n > 0) {
      memcpy(gPacket, value.c_str(), n);
    }
    gPacketLen = n;
    gPacketUpdated = n > 0;
    portEXIT_CRITICAL(&gMux);
    gLastActivityMs = millis();
  }
};

static ArrowStyle styleFor(uint8_t direction) {
  switch (direction) {
    case DirectionStraight:
    case DirectionFollow:
    case DirectionMotorway:
    case DirectionTunnel:
    case DirectionStart:
      return {SHAPE_STRAIGHT, 0};
    case DirectionKeepRight:
      return {SHAPE_BEAR, 25};
    case DirectionEasyRight:
      return {SHAPE_BEAR, 45};
    case DirectionRight:
    case DirectionExitRight:
      return {SHAPE_BEAR, 90};
    case DirectionSharpRight:
      return {SHAPE_BEAR, 135};
    case DirectionKeepLeft:
      return {SHAPE_BEAR, -25};
    case DirectionEasyLeft:
      return {SHAPE_BEAR, -45};
    case DirectionLeft:
    case DirectionExitLeft:
      return {SHAPE_BEAR, -90};
    case DirectionSharpLeft:
      return {SHAPE_BEAR, -135};
    case DirectionUTurnLeft:
      return {SHAPE_UTURN_LEFT, 0};
    case DirectionUTurnRight:
      return {SHAPE_UTURN_RIGHT, 0};
    case DirectionEnd:
      return {SHAPE_ARRIVE, 0};
    case DirectionVia:
      return {SHAPE_PIN, 0};
    case DirectionOutOfRoute:
      return {SHAPE_OFF, 0};
    case DirectionFerry:
    case DirectionStateBoundary:
      return {SHAPE_ALERT, 0};
    default:
      break;
  }

  if (direction >= DirectionRoundaboutRSE && direction <= DirectionRoundaboutLS) {
    static const int16_t bearings[] = {
        135, 90, 45, 0, -45, -90, -135, 180,
        135, 90, 45, 0, -45, -90, -135, 180,
    };
    return {SHAPE_ROUNDABOUT, bearings[direction - DirectionRoundaboutRSE]};
  }
  return {SHAPE_ALERT, 0};
}

static const char* directionLabel(uint8_t direction) {
  switch (direction) {
    case DirectionNone:
      return "Sem rota";
    case DirectionStart:
      return "Partida";
    case DirectionEasyLeft:
      return "Leve esquerda";
    case DirectionEasyRight:
      return "Leve direita";
    case DirectionEnd:
      return "Chegou";
    case DirectionVia:
      return "Ponto";
    case DirectionKeepLeft:
      return "Mantenha esq.";
    case DirectionKeepRight:
      return "Mantenha dir.";
    case DirectionLeft:
      return "Esquerda";
    case DirectionOutOfRoute:
      return "Fora da rota";
    case DirectionRight:
      return "Direita";
    case DirectionSharpLeft:
      return "Fechada esq.";
    case DirectionSharpRight:
      return "Fechada dir.";
    case DirectionStraight:
      return "Em frente";
    case DirectionUTurnLeft:
    case DirectionUTurnRight:
      return "Retorno";
    case DirectionFerry:
      return "Balsa";
    case DirectionStateBoundary:
      return "Fronteira";
    case DirectionFollow:
      return "Siga";
    case DirectionMotorway:
      return "Rodovia";
    case DirectionTunnel:
      return "Tunel";
    case DirectionExitLeft:
      return "Saida esq.";
    case DirectionExitRight:
      return "Saida dir.";
    default:
      if (direction >= DirectionRoundaboutRSE && direction <= DirectionRoundaboutLS) {
        return "Rotatoria";
      }
      return "Manobra";
  }
}

static int clampi(int value, int lo, int hi) {
  if (value < lo) return lo;
  if (value > hi) return hi;
  return value;
}

static int parseMeters(const char* text) {
  if (text == nullptr || text[0] == '\0') return -1;
  const float value = atof(text);
  if (value < 0.0f) return -1;
  for (const char* p = text; *p != '\0'; ++p) {
    if (*p == 'k' || *p == 'K') return (int)(value * 1000.0f);
  }
  return (int)value;
}

static int approachPixels(int meters) {
  if (meters < 0) meters = 220;
  if (meters > 500) meters = 500;
  return 34 + (meters * 90) / 500;
}

static void thickLine(int x0, int y0, int x1, int y1, uint16_t color) {
  for (int dx = -2; dx <= 2; ++dx) {
    lcd.drawLine(x0 + dx, y0, x1 + dx, y1, color);
  }
}

static void strokeWide(const Pt* pts, int count, int width, uint16_t color) {
  const float radius = width * 0.5f;
  for (int i = 0; i < count; ++i) {
    lcd.fillCircle(pts[i].x, pts[i].y, (int)radius, color);
  }
  for (int i = 0; i < count - 1; ++i) {
    const float dx = (float)(pts[i + 1].x - pts[i].x);
    const float dy = (float)(pts[i + 1].y - pts[i].y);
    const float len = sqrtf(dx * dx + dy * dy);
    if (len < 1.0f) continue;
    const float px = -dy / len * radius;
    const float py = dx / len * radius;
    const int x0 = pts[i].x;
    const int y0 = pts[i].y;
    const int x1 = pts[i + 1].x;
    const int y1 = pts[i + 1].y;
    lcd.fillTriangle((int)(x0 + px), (int)(y0 + py), (int)(x0 - px), (int)(y0 - py), (int)(x1 + px),
                     (int)(y1 + py), color);
    lcd.fillTriangle((int)(x0 - px), (int)(y0 - py), (int)(x1 + px), (int)(y1 + py), (int)(x1 - px),
                     (int)(y1 - py), color);
  }
}

static void drawExitHead(const Pt& from, const Pt& to) {
  float dx = (float)(to.x - from.x);
  float dy = (float)(to.y - from.y);
  const float len = sqrtf(dx * dx + dy * dy);
  if (len < 1.0f) return;
  dx /= len;
  dy /= len;
  const int tipX = to.x + (int)(dx * 16);
  const int tipY = to.y + (int)(dy * 16);
  const int px = (int)(-dy * 10);
  const int py = (int)(dx * 10);
  lcd.fillTriangle(tipX, tipY, to.x + px, to.y + py, to.x - px, to.y - py, COL_ROUTE);
}

static void paintPath(const Pt* pts, int count, bool head) {
  strokeWide(pts, count, 28, COL_ROAD);
  strokeWide(pts, count, 8, COL_ROUTE);
  if (head && count >= 2) drawExitHead(pts[count - 2], pts[count - 1]);
}

static Pt exitFrom(Pt joint, int angleDeg, int length) {
  const float rad = angleDeg * DEG_TO_RAD;
  Pt point;
  point.x = (int16_t)clampi(joint.x + (int)(sinf(rad) * length), 16, 224);
  point.y = (int16_t)clampi(joint.y - (int)(cosf(rad) * length), 12, kRiderY - 6);
  return point;
}

static void drawRider() {
  lcd.fillTriangle(kRiderX, kRiderY - 16, kRiderX - 12, kRiderY + 8, kRiderX + 12, kRiderY + 8, TFT_WHITE);
}

static void drawRoute(uint8_t direction, int meters) {
  const ArrowStyle style = styleFor(direction);
  const Pt rider{(int16_t)kRiderX, (int16_t)kRiderY};
  const Pt joint{(int16_t)kRiderX, (int16_t)(kRiderY - approachPixels(meters))};

  if (style.shape == SHAPE_OFF) {
    thickLine(70, 36, 170, 136, TFT_WHITE);
    thickLine(170, 36, 70, 136, TFT_WHITE);
    return;
  }

  if (style.shape == SHAPE_UTURN_LEFT || style.shape == SHAPE_UTURN_RIGHT) {
    const int side = style.shape == SHAPE_UTURN_LEFT ? -42 : 42;
    const Pt pts[] = {
        rider,
        joint,
        {(int16_t)clampi(joint.x + side, 16, 224), joint.y},
        {(int16_t)clampi(joint.x + side, 16, 224), (int16_t)clampi(joint.y + 36, 12, kRiderY - 6)},
    };
    paintPath(pts, 4, true);
  } else if (style.shape == SHAPE_ROUNDABOUT) {
    const Pt approach[] = {rider, {(int16_t)joint.x, (int16_t)clampi(joint.y + 24, 12, kRiderY - 6)}};
    paintPath(approach, 2, false);
    for (int radius = 18; radius <= 26; ++radius) {
      lcd.drawCircle(joint.x, joint.y, radius, COL_ROAD);
    }
    lcd.drawCircle(joint.x, joint.y, 21, COL_ROUTE);
    lcd.drawCircle(joint.x, joint.y, 22, COL_ROUTE);
    const Pt arm[] = {joint, exitFrom(joint, style.angle, 30)};
    paintPath(arm, 2, true);
  } else if (style.shape == SHAPE_ARRIVE || style.shape == SHAPE_PIN) {
    const Pt pts[] = {rider, joint};
    paintPath(pts, 2, false);
    lcd.fillCircle(joint.x, joint.y, 10, COL_ROUTE);
    lcd.fillCircle(joint.x, joint.y, 4, TFT_WHITE);
  } else {
    const int angle = style.shape == SHAPE_BEAR ? style.angle : 0;
    const Pt pts[] = {rider, joint, exitFrom(joint, angle, angle == 0 ? 28 : 44)};
    paintPath(pts, 3, true);
  }
  drawRider();
}

static void drawSpeedLimit(uint8_t speed) {
  if (speed == 0) {
    return;
  }
  const int cx = 204;
  const int cy = 28;
  lcd.fillCircle(cx, cy, 26, TFT_WHITE);
  lcd.fillCircle(cx, cy, 22, TFT_RED);
  lcd.fillCircle(cx, cy, 18, TFT_WHITE);
  lcd.setTextColor(TFT_BLACK, TFT_WHITE);
  lcd.setFont(&lgfx::fonts::Font2);
  lcd.setTextSize(1);
  lcd.setTextDatum(lgfx::textdatum_t::middle_center);
  char buf[4];
  snprintf(buf, sizeof(buf), "%u", speed);
  lcd.drawString(buf, cx, cy + 1);
}

static void drawCentered(const char* text, int y, const lgfx::IFont* font, uint8_t size, uint16_t color) {
  lcd.setFont(font);
  lcd.setTextSize(size);
  lcd.setTextColor(color, TFT_BLACK);
  lcd.setTextDatum(lgfx::textdatum_t::top_center);
  lcd.drawString(text, 120, y);
}

static void renderStatus(const char* title, const char* subtitle) {
  lcd.fillScreen(TFT_BLACK);
  drawCentered(title, 96, &lgfx::fonts::Font4, 1, TFT_WHITE);
  drawCentered(subtitle, 136, &lgfx::fonts::Font2, 1, TFT_WHITE);
}

static void renderNav(uint8_t direction, uint8_t speedLimit, const char* distance) {
  lcd.fillScreen(TFT_BLACK);
  drawRoute(direction, parseMeters(distance));
  drawCentered(directionLabel(direction), 172, &lgfx::fonts::Font2, 1, TFT_WHITE);

  if (distance != nullptr && distance[0] != '\0') {
    const uint8_t size = strlen(distance) > 8 ? 1 : 2;
    drawCentered(distance, 190, &lgfx::fonts::Font4, size, TFT_WHITE);
  }
  drawSpeedLimit(speedLimit);
  gScreen = SCREEN_NAV;
}

static void showWaiting() {
  if (gScreen == SCREEN_WAITING) {
    return;
  }
  renderStatus("BikeNav", "Aguardando Sygic");
  gScreen = SCREEN_WAITING;
}

static void showNoRoute() {
  if (gScreen == SCREEN_NOROUTE) {
    return;
  }
  renderStatus("Sem rota", "Conectado");
  gScreen = SCREEN_NOROUTE;
}

static void copyDistance(char* dst, size_t dstLen, const uint8_t* src, size_t srcLen) {
  size_t out = 0;
  for (size_t i = 0; i < srcLen && out + 1 < dstLen; ++i) {
    const char c = (char)src[i];
    if (c == '\0') {
      break;
    }
    if (c >= 32 && c <= 126) {
      dst[out++] = c;
    }
  }
  dst[out] = '\0';
}

static void logPacket(const uint8_t* data, size_t len) {
  Serial.print("HUD ");
  Serial.print(len);
  Serial.print(" bytes:");
  for (size_t i = 0; i < len; ++i) {
    char tmp[4];
    snprintf(tmp, sizeof(tmp), " %02X", data[i]);
    Serial.print(tmp);
  }
  Serial.println();
}

static bool applyPacket(const uint8_t* data, size_t len) {
  logPacket(data, len);
  if (len < 3 || data[0] != HUD_PACKET_BASIC) {
    Serial.println("pacote ignorado");
    return false;
  }

  char distance[40];
  copyDistance(distance, sizeof(distance), data + 3, len - 3);
  const uint8_t speedLimit = data[1];
  const uint8_t direction = data[2];

  if (direction == DirectionNone) {
    showNoRoute();
    return true;
  }
  renderNav(direction, speedLimit, distance);
  return true;
}

static void requestRefresh() {
  if (gIndicate == nullptr) {
    return;
  }
  const uint8_t zero = 0;
  gIndicate->setValue(&zero, 1);
  gIndicate->indicate();
}

struct DemoStep {
  uint8_t direction;
  uint8_t speedLimit;
  const char* distance;
};

static const DemoStep kDemo[] = {
    {DirectionStraight, 50, "350m"},
    {DirectionEasyRight, 50, "200m"},
    {DirectionRight, 40, "80m"},
    {DirectionSharpRight, 40, "40m"},
    {DirectionUTurnRight, 30, "20m"},
    {DirectionEasyLeft, 50, "500m"},
    {DirectionLeft, 50, "120m"},
    {DirectionSharpLeft, 30, "30m"},
    {DirectionUTurnLeft, 30, "15m"},
    {DirectionRoundaboutRE, 40, "250m"},
    {DirectionRoundaboutLW, 40, "180m"},
    {DirectionKeepLeft, 60, "1.2km"},
    {DirectionExitRight, 80, "400m"},
    {DirectionTunnel, 40, "90m"},
    {DirectionOutOfRoute, 0, ""},
    {DirectionEnd, 0, ""},
};

static void stepDemo() {
  const DemoStep& step = kDemo[gDemoIndex];
  gDemoIndex = (gDemoIndex + 1) % (sizeof(kDemo) / sizeof(kDemo[0]));
  if (step.direction == DirectionOutOfRoute) {
    renderNav(step.direction, 0, "");
    return;
  }
  renderNav(step.direction, step.speedLimit, step.distance);
}

static void handleCommand(const char* line) {
  if (strcmp(line, "demo") == 0) {
    gDemo = true;
    gDemoIndex = 0;
    gDemoMs = millis();
    gScreen = SCREEN_NONE;
    Serial.println("demo ligado");
    stepDemo();
    return;
  }
  if (strcmp(line, "stop") == 0) {
    gDemo = false;
    gScreen = SCREEN_NONE;
    Serial.println("demo desligado");
  }
}

static void pollSerial() {
  static char line[24];
  static uint8_t len = 0;
  while (Serial.available() > 0) {
    const char c = (char)Serial.read();
    if (c == '\r') {
      continue;
    }
    if (c == '\n') {
      line[len] = '\0';
      if (len > 0) {
        handleCommand(line);
      }
      len = 0;
      continue;
    }
    if (len + 1 < sizeof(line)) {
      line[len++] = c;
    }
  }
}

static void startBle() {
  BLEDevice::init(HUD_DEVICE_NAME);
  BLEServer* server = BLEDevice::createServer();
  server->setCallbacks(new HudServerCallbacks());

  BLEService* service = server->createService(HUD_SERVICE_UUID);

  gIndicate = service->createCharacteristic(HUD_CHAR_INDICATE_UUID, BLECharacteristic::PROPERTY_INDICATE);
  gIndicate->addDescriptor(new BLE2902());
  const uint8_t zero = 0;
  gIndicate->setValue(&zero, 1);

  BLECharacteristic* writeChar =
      service->createCharacteristic(HUD_CHAR_WRITE_UUID, BLECharacteristic::PROPERTY_WRITE);
  writeChar->setCallbacks(new HudWriteCallbacks());

  service->start();

  BLEAdvertising* advertising = BLEDevice::getAdvertising();
  advertising->addServiceUUID(HUD_SERVICE_UUID);
  advertising->setScanResponse(true);
  advertising->setMinPreferred(0x06);
  advertising->setMinPreferred(0x12);
  BLEDevice::startAdvertising();
  Serial.println("anunciando ESP32 HUD");
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println("BikeNav");
  Serial.println("serial: demo | stop");

  lcd.init();
  lcd.setRotation(TFT_ROTATION);
  lcd.setBrightness(TFT_BRIGHTNESS);
  lcd.fillScreen(TFT_BLACK);
  Serial.printf("heap livre %u\n", (unsigned)ESP.getFreeHeap());

  showWaiting();
  startBle();
}

void loop() {
  pollSerial();

  uint8_t local[HUD_PACKET_MAX];
  size_t len = 0;
  bool updated = false;
  portENTER_CRITICAL(&gMux);
  if (gPacketUpdated) {
    len = gPacketLen;
    memcpy(local, gPacket, len);
    gPacketUpdated = false;
    updated = true;
  }
  portEXIT_CRITICAL(&gMux);

  if (updated) {
    gDemo = false;
    applyPacket(local, len);
  }

  if (gDemo) {
    if (millis() - gDemoMs > 1200) {
      gDemoMs = millis();
      stepDemo();
    }
    delay(10);
    return;
  }

  if (!gConnected) {
    showWaiting();
    delay(10);
    return;
  }

  if (gScreen == SCREEN_WAITING || gScreen == SCREEN_NONE) {
    showNoRoute();
  }

  const uint32_t now = millis();
  if (now - gLastActivityMs > HUD_INDICATE_INTERVAL_MS) {
    gLastActivityMs = now;
    requestRefresh();
  }
  delay(10);
}
