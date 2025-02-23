// Copyright (c) Sandeep Mistry. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include <esp32_can.h>  // the ESP32_OBD2 library depends on the https://github.com/collin80/esp32_can and https://github.com/collin80/can_common CAN libraries
#include <esp32_obd2.h>

#define CAN_RX_PIN 14
#define CAN_TX_PIN 13

// array of PID's to print values of
const int PIDS[] = {
  CALCULATED_ENGINE_LOAD,
  ENGINE_COOLANT_TEMPERATURE,
  ENGINE_RPM,
  VEHICLE_SPEED,
  AIR_INTAKE_TEMPERATURE,
  MAF_AIR_FLOW_RATE,
  THROTTLE_POSITION,
  RUN_TIME_SINCE_ENGINE_START,
  FUEL_TANK_LEVEL_INPUT,
  ABSOLULTE_BAROMETRIC_PRESSURE,
  ABSOLUTE_LOAD_VALUE,
  RELATIVE_THROTTLE_POSITION
};

const int NUM_PIDS = sizeof(PIDS) / sizeof(PIDS[0]);

void setup() {
  Serial.begin(115200);
  while (!Serial)
    ;

  Serial.println(F("OBD2 Supported PIDs"));

  CAN0.setCANPins(GPIO_NUM_14, GPIO_NUM_13);
  CAN0.begin(500000);
  OBD2.begin();
  // loop through PIDs 0 to 95, reading and printing the names of the supported PIDs
  for (int pid = 0; pid < 96; pid++) {
    if (OBD2.pidSupported(pid)) {
      Serial.println(OBD2.pidName(pid));
    }
  }
}

void loop() {

  for (int i = 0; i < NUM_PIDS; i++) {
    int pid = PIDS[i];

    printPID(pid);
  }
  Serial.println();

  delay(1000);
}

void printPID(int pid) {
  // print PID name
  Serial.print(OBD2.pidName(pid));
  Serial.print(F(" = "));

  // read the PID value
  float pidValue = OBD2.pidRead(pid);

  if (isnan(pidValue)) {
    Serial.print("error");
  } else {
    // print value with units
    Serial.print(pidValue);
    Serial.print(F(" "));
    Serial.print(OBD2.pidUnits(pid));
  }

  Serial.println();
}

void printFrame(CAN_FRAME *message) {
  Serial.print(message->id, HEX);
  if (message->extended) Serial.print(" X ");
  else Serial.print(" S ");
  Serial.print(message->length, DEC);
  Serial.print(" ");
  for (int i = 0; i < message->length; i++) {
    Serial.print(message->data.byte[i], HEX);
    Serial.print(" ");
  }
  Serial.println();
}