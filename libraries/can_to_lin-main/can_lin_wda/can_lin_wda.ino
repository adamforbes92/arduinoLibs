/*
 * Brief:       A module to allow CAN messages to control a Bosch WDA
 * Author:      Sander
 */ 

#include "lin_bus.h"

bool DEBUG_MODE = true;

LIN lin;

int lin_cs = 32;
int lin_fault = 28;
int led1 = 13;

int wda_counter = 0;  // Bosch WDA 0-15 rolling counter
int wda_kl15_enable = 1;  // Bosch WDA KL15 ignition enable (ON REQUIRED)
int wda_klx_enable = 1;  // Bosch WDA KLX enable (ON REQUIRED)

uint8_t wda_intermittent_wipe_speed = 5;  // Bosch WDA speed controls for intermittent wipe
/*
  * 1 = slow
  * 5 = low-mid
  * 9 = high-mid
  * 13 = = fast
  */
int wda_single_wipe_req = 0;  // Bosch WDA single wipe
int wda_intermittent_wipe_req = 0;  // Bosch WDA intermittent wipe
int wda_cont_slow_wipe_req = 0;  // Bosch WDA slow continous wipe
int wda_cont_fast_wipe_req = 1;  // Bosch WDA fast continous wipe

uint8_t buffer_data[] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

void setup() {
  pinMode(lin_cs, OUTPUT);
  digitalWrite(lin_cs, HIGH);
  pinMode(lin_fault, INPUT);
  lin.begin(&Serial3, 19200);
  Serial.println("LIN has been enabled");

  Serial.begin(115200);
}

void loop() {
  wda_counter++;

  if (wda_counter > 15) {
    wda_counter = 0;
  }

  buffer_data[0] = (buffer_data[0] & 0xF0) | (wda_counter & 0x0F);

  if (wda_kl15_enable == 1) {
    buffer_data[0] |= (1 << 4);  // Set bit 4
  } else {
    buffer_data[0] &= ~(1 << 4);  // Clear bit 4
  }

  if (wda_klx_enable == 1) {
    buffer_data[0] |= (1 << 5);  // Set bit 5
  } else {
    buffer_data[0] &= ~(1 << 5);  // Clear bit 5
  }

  buffer_data[1] = (buffer_data[1] & 0xF0) | (wda_intermittent_wipe_speed & 0x0F);

  if (wda_single_wipe_req == 1) {
    buffer_data[1] |= (1 << 4);  // Set bit 4
  } else {
    buffer_data[1] &= ~(1 << 4);  // Clear bit 4
  }

  if (wda_intermittent_wipe_req == 1) {
    buffer_data[1] |= (1 << 5);  // Set bit 5
  } else {
    buffer_data[1] &= ~(1 << 5);  // Clear bit 5
  }

  if (wda_cont_slow_wipe_req == 1) {
    buffer_data[1] |= (1 << 6);  // Set bit 6
  } else {
    buffer_data[1] &= ~(1 << 6);  // Clear bit 6
  }

  if (wda_cont_fast_wipe_req == 1) {
    buffer_data[1] |= (1 << 7);  // Set bit 7
  } else {
    buffer_data[1] &= ~(1 << 7);  // Clear bit 7
  }

  lin.order(0x31, buffer_data, 8, lin2x);
  Serial.println("Raw data to wda: ");
  Serial.println((buffer_data[0] & 0xf0) >> 4);
  Serial.println((buffer_data[0] & 0x0f));  
  Serial.println((buffer_data[1] & 0xf0) >> 4);
  Serial.println((buffer_data[1] & 0x0f));  
  Serial.println();

  delay(20);  // Sleep
}
