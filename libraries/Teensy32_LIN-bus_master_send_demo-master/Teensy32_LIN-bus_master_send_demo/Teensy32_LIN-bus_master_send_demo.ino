/*
 * This is a Teensy 3.2 LIN-Bus demo using this board:
 * http://skpang.co.uk/catalog/teensy-canbus-and-linbus-breakout-board-include-teensy-32-p-1566.html
 * 
 * It is used to control the Microchip APGRD004 LIN RGB LED.
 * 
 * skpang.co.uk 2019
 * 
 * 
 */
#include "lin-bus.h"

int led = 13;
int lin_cs = 23;
#define pinTX 16  // transmit pin for LIN
#define pinRX 17  // receive pin for LIN

#define pinWake 18  // wake for LIN
#define pinCS 19    // chip select for LIN
lib_bus lin(BAUD_19200, 1);
uint8_t frameCount = 0;                                                     // Counter for wiper LIN frame count.  0 to 15
uint8_t wiperFrame[] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };  // For wiper frame output
uint8_t intervalSpeed = 1;                                                  // Define interval speed (1=1, 2=5, 3=9, 4=13) ** CAN CHANGE THIS **
bool wiperSingle = false;                                                   // Bool for single wipe
bool wiperInt = false;                                                      // Bool for intermittent
bool wiperPos1 = false;                                                     // Bool for position 1
bool wiperPos2 = false;                                                     // Bool for position 2

void setup() {
  pinMode(pinCS, OUTPUT);
  pinMode(pinWake, OUTPUT);
  digitalWrite(pinCS, HIGH);
  digitalWrite(pinWake, HIGH);

  pinMode(pinTX, INPUT);
  pinMode(pinRX, INPUT);

  Serial.begin(115200);  // Configure serial port for debug
  Serial.print("LIN-bus test");
  //Serial2.begin(19200, SERIAL_8N1, 17, 16);  //Hardware Serial of ESP32

  delay(100);
  wiperPos2 = true;
}

void loop() {
  delay(20);

  frameCount++;
  if (frameCount > 15) {
    frameCount = 0;
  }
  wiperFrame[0] = (wiperFrame[0] & 0xF0) | (frameCount & 0x0F);
  // set individual 'bits' on the frame for KL terms (fixed - if we have power, the wipers should be 'active')
  bitWrite(wiperFrame[0], 4, 1);  // KL.15 (Forced 1) - assumed power, so use wipers
  bitWrite(wiperFrame[0], 5, 1);  // KL.X (Forced 1) - assumed power, so use wipers
  bitWrite(wiperFrame[0], 6, 0);  // 0
  bitWrite(wiperFrame[0], 7, 0);  // 0

  // Frame 1
  // set interval speed from static number stored at top (1=1, 2=5, 3=9, 4=13)
  wiperFrame[1] = (wiperFrame[1] & 0xF0) | (intervalSpeed & 0x0F);
  // set individual 'bits' on the frame for wiper direction (based on inputs): single strike, intermittent, SPD1, SPD2
  bitWrite(wiperFrame[1], 4, 0);  // single strike
  bitWrite(wiperFrame[1], 5, 0);  // intermittent
  bitWrite(wiperFrame[1], 6, 0);  // SPD1
  bitWrite(wiperFrame[1], 7, 1);  // SPD2

  // Frame 3 through 5 are 'empty' / not used, so send over 0x00
  // empty frames
  wiperFrame[2] = 0x00;  // empty, empty, empty, empty, empty
  wiperFrame[3] = 0x00;  // empty, empty, empty, empty, empty
  wiperFrame[4] = 0x00;  // empty, empty, empty, empty, empty
  wiperFrame[5] = 0x00;  // empty, empty, empty, empty, empty
  wiperFrame[6] = 0x00;  // empty, empty, empty, empty, empty
  wiperFrame[7] = 0x00;  // empty, empty, empty, empty, empty
  //lin.write(0x31, wiperFrame, 8);

  // write wiper motor LIN Diag / Output
  //Serial.println("");
  //printBits(wiperFrame[0]);
}

void printBits(byte myByte) {
  // for Serial debug - used to check the binary leaving...
  for (byte mask = 0x80; mask; mask >>= 1) {
    if (mask & myByte)
      Serial.print('1');
    else
      Serial.print('0');
  }
}
