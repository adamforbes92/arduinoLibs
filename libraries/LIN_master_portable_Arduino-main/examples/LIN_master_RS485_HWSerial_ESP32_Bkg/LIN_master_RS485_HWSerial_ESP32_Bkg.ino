/*********************

Example code for LIN master node with background operation using HardwareSerial.
Physical interface is RS485 (e.g. MAX485) with Tx direction switching. Permanently enable Rx (REN=GND) for receiving echo

This code runs a LIN master node in "background" operation using HardwareSerial interface

Note: after starting a frame, LIN.handler() must be called every <=500us at least until state has changed from STATE_BREAK to STATE_BODY

Supported (=successfully tested) boards:
 - ESP32 Wroom-32UE       https://www.etechnophiles.com/esp32-dev-board-pinout-specifications-datasheet-and-schematic/

**********************/

// include files
#include "LIN_master_HardwareSerial_ESP32.h"


// LIN transmit pin
#define PIN_LIN_TX    17

// LIN receive pin
#define PIN_LIN_RX    16

// pin to switch RS485 Tx direction (=DE)
#define PIN_TXEN      21

// pin to demonstrate background operation
#define PIN_TOGGLE    19

// indicate LIN return status
#define PIN_ERROR     18

// pause between LIN frames
#define LIN_PAUSE     200

// serial I/F for debug output (comment for no output)
#define SERIAL_DEBUG  Serial


// setup LIN node
LIN_Master_HardwareSerial_ESP32   LIN(Serial2, PIN_LIN_RX, PIN_LIN_TX, "Master", PIN_TXEN);


// call once
void setup()
{
  // for debug output
  #if defined(SERIAL_DEBUG)
    SERIAL_DEBUG.begin(115200);
    while(!SERIAL_DEBUG);
  #endif // SERIAL_DEBUG

  // indicate background operation
  pinMode(PIN_TOGGLE, OUTPUT);

  // indicate LIN status via pin
  pinMode(PIN_ERROR, OUTPUT);

  // open LIN interface
  LIN.begin(19200);

} // setup()


// call repeatedly
void loop()
{
  static uint32_t           lastLINFrame = 0;
  static uint8_t            count = 0;
  uint8_t                   Tx[4] = {0x01, 0x02, 0x03, 0x04};
  LIN_Master_Base::frame_t  Type;
  LIN_Master_Base::error_t  error;
  uint8_t                   Id;
  uint8_t                   NumData;
  uint8_t                   Data[8];
  

  ///////////////
  // as fast as possible
  ///////////////
  
  // toggle pin to show background operation
  digitalWrite(PIN_TOGGLE, !digitalRead(PIN_TOGGLE));

  // call LIN background handler
  LIN.handler();


  ///////////////
  // check if LIN frame has finished
  ///////////////
  if (LIN.getState() == LIN_Master_Base::STATE_DONE)
  {
    // get frame data & error status
    LIN.getFrame(Type, Id, NumData, Data);
    error = LIN.getError();

    // indicate status via pin
    digitalWrite(PIN_ERROR, error);

    // print result
    #if defined(SERIAL_DEBUG)
      if (Type == LIN_Master_Base::MASTER_REQUEST)
      {
        SERIAL_DEBUG.print(LIN.nameLIN);
        SERIAL_DEBUG.print(", request, ID=0x");
        SERIAL_DEBUG.print(Id, HEX);
        if (error != LIN_Master_Base::NO_ERROR)
        { 
          SERIAL_DEBUG.print(", err=0x");
          SERIAL_DEBUG.println(error, HEX);
        }
        else
        {
          SERIAL_DEBUG.print(", data=");        
          for (uint8_t i=0; (i < NumData); i++)
          {
            SERIAL_DEBUG.print("0x");
            SERIAL_DEBUG.print((int) Data[i], HEX);
            SERIAL_DEBUG.print(" ");
          }
          SERIAL_DEBUG.println();
        }
      }
      else
      {
        SERIAL_DEBUG.print(LIN.nameLIN);
        SERIAL_DEBUG.print(", response, ID=0x");
        SERIAL_DEBUG.print(Id, HEX);
        if (error != LIN_Master_Base::NO_ERROR)
        { 
          SERIAL_DEBUG.print(", err=0x");
          SERIAL_DEBUG.println(error, HEX);
        }
        else
        {
          SERIAL_DEBUG.print(", data=");        
          for (uint8_t i=0; (i < NumData); i++)
          {
            SERIAL_DEBUG.print("0x");
            SERIAL_DEBUG.print((int) Data[i], HEX);
            SERIAL_DEBUG.print(" ");
          }
          SERIAL_DEBUG.println();
        }
      }
    #endif // SERIAL_DEBUG

    // reset state machine & error
    LIN.resetStateMachine();
    LIN.resetError();

  } // if LIN frame finished


  ///////////////
  // SW scheduler for sending/receiving LIN frames
  ///////////////
  if (millis() - lastLINFrame > LIN_PAUSE)
  {
    lastLINFrame = millis();

    // send master request frame (background)
    if (count == 0)
    {
      count++;
      LIN.sendMasterRequest(LIN_Master_Base::LIN_V2, 0x1A, 4, Tx);
    }


    // send slave response frame (background)
    else
    {
      count = 0;
      LIN.receiveSlaveResponse(LIN_Master_Base::LIN_V2, 0x05, 6);
    }
    
  } // SW scheduler

} // loop()
