#include <Arduino.h>
#include "SteeringWheelManager.h"
#include "GamepadManager.h"

constexpr uint8_t PIN_LIN_RX = 17;
constexpr uint8_t PIN_LIN_TX = 16;
constexpr uint8_t PIN_LIN_NSLP = 18;
constexpr uint32_t LIN_BAUD = 19200;
constexpr uint8_t HORN_PIN = 5;

constexpr const char* GAMEPAD_DEVICE_NAME = "VW Steering Wheel";
constexpr const char* GAMEPAD_MANUFACTURER_NAME = "Henrique Nunes";

SteeringWheelManager steeringWheelManager(PIN_LIN_RX, PIN_LIN_TX, PIN_LIN_NSLP, LIN_BAUD, HORN_PIN);
GamepadManager gamepadManager(GAMEPAD_DEVICE_NAME, GAMEPAD_MANUFACTURER_NAME);

void setup() {
  Serial.begin(115200);
  pinMode(19, OUTPUT);

  // drive CS & Wake high to use the LIN chip
  digitalWrite(19, HIGH);
}

void loop() {
  bool isGamepadConnected = gamepadManager.isConnected();

  steeringWheelManager.flashBacklights(isGamepadConnected);

  if (!isGamepadConnected) {
    return;
  }

  byte* buttonStates = steeringWheelManager.readButtonStates();
  if (buttonStates != nullptr) {
    gamepadManager.update(buttonStates);
  }
}
