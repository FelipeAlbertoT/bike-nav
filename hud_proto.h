#pragma once

#include <stdint.h>

// BLE HUD do Sygic. O app varre este servico e conecta no primeiro aparelho.
// Mesmos UUIDs de alexanderlavrushko/BLE-HUD-navigation-ESP32.
#define HUD_SERVICE_UUID "DD3F0AD1-6239-4E1F-81F1-91F6C9F01D86"
#define HUD_CHAR_INDICATE_UUID "DD3F0AD2-6239-4E1F-81F1-91F6C9F01D86"
#define HUD_CHAR_WRITE_UUID "DD3F0AD3-6239-4E1F-81F1-91F6C9F01D86"

// Nome que o firmware de referencia anuncia. O Sygic acha o aparelho pelo UUID.
#define HUD_DEVICE_NAME "ESP32 HUD"

// Pacote: [0x01][limite km/h][direcao][texto ASCII ate o fim, ex. "350m"]
#define HUD_PACKET_BASIC 0x01
#define HUD_PACKET_MAX 64
#define HUD_INDICATE_INTERVAL_MS 4000

// Codigos que o Sygic grava na caracteristica BLE.
// O exemplo do autor (0x0A = direita) bate com este enum, nao com a API websocket.
enum HudDirection : uint8_t {
  DirectionNone = 0,
  DirectionStart = 1,
  DirectionEasyLeft = 2,
  DirectionEasyRight = 3,
  DirectionEnd = 4,
  DirectionVia = 5,
  DirectionKeepLeft = 6,
  DirectionKeepRight = 7,
  DirectionLeft = 8,
  DirectionOutOfRoute = 9,
  DirectionRight = 10,
  DirectionSharpLeft = 11,
  DirectionSharpRight = 12,
  DirectionStraight = 13,
  DirectionUTurnLeft = 14,
  DirectionUTurnRight = 15,
  DirectionFerry = 16,
  DirectionStateBoundary = 17,
  DirectionFollow = 18,
  DirectionMotorway = 19,
  DirectionTunnel = 20,
  DirectionExitLeft = 21,
  DirectionExitRight = 22,
  DirectionRoundaboutRSE = 23,
  DirectionRoundaboutRE = 24,
  DirectionRoundaboutRNE = 25,
  DirectionRoundaboutRN = 26,
  DirectionRoundaboutRNW = 27,
  DirectionRoundaboutRW = 28,
  DirectionRoundaboutRSW = 29,
  DirectionRoundaboutRS = 30,
  DirectionRoundaboutLSE = 31,
  DirectionRoundaboutLE = 32,
  DirectionRoundaboutLNE = 33,
  DirectionRoundaboutLN = 34,
  DirectionRoundaboutLNW = 35,
  DirectionRoundaboutLW = 36,
  DirectionRoundaboutLSW = 37,
  DirectionRoundaboutLS = 38,
};
