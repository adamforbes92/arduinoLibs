int ledState = LOW;       // ledState used to set the LED
long previousMillis = 0;  // will store last time LED was updated

long interval = 250;

#define pinTX 16    // transmit pin for LIN
#define pinRX 17    // receive pin for LIN
#define pinWake 18  // wake for LIN
#define pinCS 19    // chip select for LIN

//Lin Initailisation
#define linspeed 19200
unsigned long Tbit = 1000000 / linspeed;

#define uartlenght 10

//Tbits Header
#define breakfield 13
#define breakdelimiter 1
#define breakfieldinterbytespace 2

int frameerrordelay = ((breakfield + breakdelimiter) - uartlenght) * Tbit;
//int frameerrordelay = (breakfield + breakdelimiter + breakfieldinterbytespace) - uartlenght;

#define syncfield uartlenght
#define PIDfield uartlenght
#define syncfieldPIDinterbytedelay 0
int syncfieldPIDinterbytespace = syncfieldPIDinterbytedelay * Tbit;
int frameCount = 0;
int intervalSpeed = 5;

//Tbit Response
#define responsedelay 8
int responsespace = responsedelay * Tbit;
#define interbytedelay 0
int interbytespace = interbytedelay * Tbit;

#define numbers 6
byte message[numbers], sending[numbers];
byte linb = 0, idbyte = 0, PID = 0, checksum = 0;
int n = 0;

void setup() {
  // initialize serial port and LIN:
  Serial.begin(115200);
    // setup the pins for input/output
  pinMode(pinCS, OUTPUT);
  pinMode(pinWake, OUTPUT);

  // drive CS & Wake high to use the LIN chip
  digitalWrite(pinCS, HIGH);
  digitalWrite(pinWake, HIGH);

  Lininit();
}

void loop() {
  frameCount++;
  if (frameCount > 15) {
    frameCount = 0;
  }
  sending[0] = frameCount;  // Bit 0-3: Counter.  Bit 4: KL.15. Bit 5: KL.X. Bit 6: 0. Bit 7: 0.
  // set individual 'bits' on the frame for KL terms (fixed - if we have power, the wipers should be 'active')
  bitWrite(sending[0], 4, 1);  // KL.15 (Forced 1) - assumed power, so use wipers
  bitWrite(sending[0], 5, 1);  // KL.X (Forced 1) - assumed power, so use wipers
  bitWrite(sending[0], 6, 0);  // 0
  bitWrite(sending[0], 7, 0);  // 0

  // Frame 1
  // set interval speed from static number stored at top (1=1, 2=5, 3=9, 4=13)
  sending[1] = intervalSpeed;  // Bit 0-3: Counter.  Bit 4: KL.15. Bit 5: KL.X. Bit 6: 0. Bit 7: 0.
  // set individual 'bits' on the frame for wiper direction (based on inputs): single strike, intermittent, SPD1, SPD2
  bitWrite(sending[1], 4, 0);  // single strike
  bitWrite(sending[1], 5, 0);     // intermittent
  bitWrite(sending[1], 6, 0);    // SPD1
  bitWrite(sending[1], 7, 1);    // SPD2

  // Frame 3 through 5 are 'empty' / not used, so send over 0x00
  // empty frames
  sending[2] = 0x00;  // empty, empty, empty, empty, empty
  sending[3] = 0x00;  // empty, empty, empty, empty, empty
  sending[4] = 0x00;  // empty, empty, empty, empty, empty
  sending[5] = 0x00;  // empty, empty, empty, empty, empty

  LinWriting();
}

void LinWriting() {
  LinWrite(0x31, sending);
  Answer();
  delay(40);
}

void Lininit() {
  Serial2.begin(linspeed, SERIAL_8N1, pinRX, pinTX);
}

void Answer() {
  for (int i = 0; i < numbers; i++) {
    sending[i] = n;
  }
  n++;
  n = n % 256;
  Serial.println(n, HEX);
}

void LinWrite(byte PID, byte* sending) {
  Serial2.end();
  pinMode(pinTX, OUTPUT);
  digitalWrite(pinTX, LOW);
  delayMicroseconds(breakfield * Tbit);  //after Frame Error Tbit to Sync Field
  digitalWrite(pinTX, HIGH);
  delayMicroseconds(breakdelimiter * Tbit);

  Serial2.begin(linspeed, SERIAL_8N1, pinRX, pinTX);
  Serial2.write(0x55);
  Serial2.write(PID& 0x3F | PIDCRC(PID) << 6);
  delayMicroseconds(responsespace);

  for (int i = 0; i < numbers; i++) {
    Serial2.write(sending[i]);
    if (interbytespace == 0) {
      delayMicroseconds(1);
    } else {
      delayMicroseconds(interbytespace);
    }
  }
  Serial2.write(MessageCRC(sending, PID));
}

int PIDCRC(int PID) {
  int P0 = ((PID >> 0) + (PID >> 1) + (PID >> 2) + (PID >> 4)) & 1;
  int P1 = ~((PID >> 1) + (PID >> 3) + (PID >> 4) + (PID >> 5)) & 1;
  return (P0 | (P1 << 1));
}

byte MessageCRC(byte* message, uint16_t sum) {
  for (int i = 0; i < numbers; i++) {
    sum += message[i];
  }
  while (sum >> 8)  // In case adding the carry causes another carry
    sum = (sum & 255) + (sum >> 8);
  return (~sum);
}
