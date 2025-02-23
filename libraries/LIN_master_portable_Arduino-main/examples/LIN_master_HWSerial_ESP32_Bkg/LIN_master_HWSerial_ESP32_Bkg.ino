/*********************

Example code for LIN master node with background operation using HardwareSerial

This code runs a LIN master node in "background" operation using HardwareSerial interface

Note: after starting a frame, LIN.handler() must be called every <=500us at least until state has changed from STATE_BREAK to STATE_BODY

Supported (=successfully tested) boards:
 - ESP32 Wroom-32UE       https://www.etechnophiles.com/esp32-dev-board-pinout-specifications-datasheet-and-schematic/

**********************/

// include files
#include "LIN_master_HardwareSerial_ESP32.h"


// LIN transmit pin
#define PIN_LIN_TX 16

// LIN receive pin
#define PIN_LIN_RX 17

// pin to demonstrate background operation
#define PIN_TOGGLE 21

// indicate LIN return status
#define PIN_ERROR 22

// pause between LIN frames
#define LIN_PAUSE 200

// serial I/F for debug output (comment for no output)
#define SERIAL_DEBUG Serial

#define pinWake 18  // wake for LIN
#define pinCS 19    // chip select for LIN
uint32_t i = 0;
uint32_t j = 0;

// setup LIN node
LIN_Master_HardwareSerial_ESP32 LIN(Serial2, PIN_LIN_RX, PIN_LIN_TX, "Master");


// call once
void setup() {
// for debug output
#if defined(SERIAL_DEBUG)
  SERIAL_DEBUG.begin(115200);
  while (!SERIAL_DEBUG)
    ;
#endif  // SERIAL_DEBUG

  // indicate background operation
  pinMode(PIN_TOGGLE, OUTPUT);

  // indicate LIN status via pin
  pinMode(PIN_ERROR, OUTPUT);
  // setup the pins for input/output
  pinMode(pinCS, OUTPUT);
  pinMode(pinWake, OUTPUT);

  // drive CS & Wake high to use the LIN chip
  digitalWrite(pinCS, HIGH);
  digitalWrite(pinWake, HIGH);
  // open LIN interface
  LIN.begin(9600);

}  // setup()


// call repeatedly
void loop() {
  j++;
  if (j >= 80000) {
    i++;
    j = 0;
    Serial.println(i);
  }
  if (i >= 255) {
    i = 0;
  }

  static uint32_t lastLINFrame = 0;
  static uint8_t count = 0;
  uint8_t Tx[4] = { 0xA2, i, 0x03, 0x00 };
  LIN_Master_Base::frame_t Type;
  LIN_Master_Base::error_t error;
  uint8_t Id;
  uint8_t NumData;
  uint8_t Data[8];


  ///////////////
  // as fast as possible
  ///////////////
  // call LIN background handler
  LIN.handler();


  ///////////////
  // check if LIN frame has finished
  ///////////////
  if (LIN.getState() == LIN_Master_Base::STATE_DONE) {
    // get frame data & error status
    LIN.getFrame(Type, Id, NumData, Data);
    error = LIN.getError();

    // reset state machine & error
    LIN.resetStateMachine();
    LIN.resetError();

  }  // if LIN frame finished


  ///////////////
  // SW scheduler for sending/receiving LIN frames
  ///////////////
  if (millis() - lastLINFrame > LIN_PAUSE) {
    lastLINFrame = millis();

    // send master request frame (background)
    if (count == 0) {
      count++;
      LIN.sendMasterRequest(LIN_Master_Base::LIN_V2, 0x20, 4, Tx);
    }
    // send slave response frame (background)
    else {
      count = 0;
      LIN.receiveSlaveResponse(LIN_Master_Base::LIN_V2, 0x05, 6);
    }

  }  // SW scheduler

}  // loop()
