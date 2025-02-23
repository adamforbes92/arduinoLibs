/*
 InterruptOneButton.ino - Example for the OneButtonLibrary library.
 This is a sample sketch to show how to use the OneButtonLibrary
 to detect double-click events on a button by using interrupts. 
 The library internals are explained at
 http://www.mathertel.de/Arduino/OneButtonLibrary.aspx

 Good article on Arduino UNO interrupts:
 https://arduino.stackexchange.com/questions/30968/how-do-interrupts-work-on-the-arduino-uno-and-similar-boards
 ... and the processor datasheet.
  
 Setup a test circuit:
 * Connect a pushbutton to the PIN_INPUT (see defines for processor specific examples) and ground.
 * The PIN_LED (see defines for processor specific examples) is used for output attach a led and resistor to VCC.
   or maybe use a built-in led on the standard arduino board.
   
 The sketch shows how to setup the library and bind the functions (singleClick, doubleClick) to the events.
 In the loop function the button.tick function must be called as often as you like.
 By using interrupts the internal state advances even when for longer time the button.tick is not called.
*/

// 03.03.2011 created by Matthias Hertel
// 01.12.2011 extension changed to work with the Arduino 1.0 environment
// 04.11.2017 Interrupt version created.
// 04.11.2017 Interrupt version using attachInterrupt.

#include "OneButton.h"

#define stalkPushUp 36     // input stalk UP
#define stalkPushDown 39   // input stalk DOWN
#define stalkPushReset 34  // input stalk RESET

// Attach a LED using GPIO 25 and VCC. The LED is on when output level is LOW.
#define PIN_LED 25

// Setup a new OneButton on pin PIN_INPUT
// The 2. parameter activeLOW is true, because external wiring sets the button to LOW when pressed.
extern OneButton stalkUpButton(stalkPushUp, true);
extern OneButton stalkDownButton(stalkPushDown, true);
extern OneButton stalkResetButton(stalkPushReset, true);

// current LED state, staring with LOW (0)
int ledState = LOW;

// save the millis when a press has started.
unsigned long pressStartTime;

// In case the momentary button puts the input to HIGH when pressed:
// The 2. parameter activeLOW is false when the external wiring sets the button to HIGH when pressed.
// The 3. parameter can be used to disable the PullUp .
// OneButton button(PIN_INPUT, false, false);


void IRAM_ATTR checkTicks() {
  // include all buttons here to be checked
  stalkUpButton.tick();     // just call tick() to check the state.
  stalkDownButton.tick();   // just call tick() to check the state.
  stalkResetButton.tick();  // just call tick() to check the state
}

// this function will be called when the button was pressed 1 time only.
void singleClick() {
  Serial.println("singleClick() detected.");
}  // singleClick

// this function will be called when the button was pressed 2 times in a short timeframe.
void doubleClick() {
  Serial.println("doubleClick() detected.");

  ledState = !ledState;  // reverse the LED
  digitalWrite(PIN_LED, ledState);
}  // doubleClick


// this function will be called when the button was pressed multiple times in a short timeframe.
void multiClick() {
  int n = stalkUpButton.getNumberClicks();
  if (n == 3) {
    Serial.println("tripleClick detected.");
  } else if (n == 4) {
    Serial.println("quadrupleClick detected.");
  } else {
    Serial.print("multiClick(");
    Serial.print(n);
    Serial.println(") detected.");
  }

  ledState = !ledState;  // reverse the LED
  digitalWrite(PIN_LED, ledState);
}  // multiClick


// this function will be called when the button was held down for 1 second or more.
void pressStart() {
  Serial.println("pressStart()");
  pressStartTime = millis() - 1000;  // as set in setPressMs()
}  // pressStart()


// this function will be called when the button was released after a long hold.
void pressStop() {
  Serial.print("pressStop(");
  Serial.print(millis() - pressStartTime);
  Serial.println(") detected.");
}  // pressStop()


// setup code here, to run once:
void setup() {
  Serial.begin(115200);
  Serial.println("One Button Example with interrupts.");

  // enable the led output.
  pinMode(PIN_LED, OUTPUT);  // sets the digital pin as output
  digitalWrite(PIN_LED, ledState);

  // setup interrupt routine
  // when not registering to the interrupt the sketch also works when the tick is called frequently.
  attachInterrupt(digitalPinToInterrupt(stalkPushUp), checkTicks, CHANGE);
  attachInterrupt(digitalPinToInterrupt(stalkPushDown), checkTicks, CHANGE);
  attachInterrupt(digitalPinToInterrupt(stalkPushReset), checkTicks, CHANGE);

  // link the xxxclick functions to be called on xxxclick event.
  stalkUpButton.attachClick(singleClick);
  stalkUpButton.setPressMs(1000);  // that is the time when LongPressStart is called
  stalkDownButton.attachClick(singleClick);
  stalkDownButton.setPressMs(1000);  // that is the time when LongPressStart is called
  stalkResetButton.attachClick(singleClick);
  stalkResetButton.setPressMs(1000);  // that is the time when LongPressStart is called

}  // setup

// main code here, to run repeatedly:
void loop() {
  // keep watching the push button, even when no interrupt happens:
  stalkUpButton.tick();
  stalkDownButton.tick();
  stalkResetButton.tick();
  // You can implement other code in here or just wait a while
  delay(250);
}  // loop


// End
