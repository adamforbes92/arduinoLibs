
#include "lin-bus.h"

#define BAUD_19200_CONST 695  //Baudrate to generate line break at 19200
#define BAUD_9600_CONST 660   //Baudrate to generate line break at 9600

lib_bus::lib_bus(uint16_t baudrate, uint8_t Tx_pin) {
  tx_pin = Tx_pin;
  kbps = baudrate;
}
int lib_bus::write(uint8_t ident, uint8_t data[], uint8_t data_size) {

  uint8_t addrbyte = (ident & 0x3f) | addrParity(ident);
  addrbyte = ((ident & 0x3F) | (addrParity(ident) << 6));

  uint8_t cksum = dataChecksum(data, data_size, addrbyte);

  //Serial2.begin(19200, SERIAL_8N1, 17, 16);
  //Serial2.begin(kbps);  // Configure baudrate

  Serial2.write(0x55);  // write Synch Byte to serial
  Serial2.flush();

  Serial2.write(addrbyte);  // write Identification Byte to serial
  delayMicroseconds(104);

  for (uint8_t i = 0; i < data_size; i++) {
    Serial2.write(data[i]);  // write data to serial
  }
  Serial2.write(cksum);
  Serial2.flush();

  //Serial2.end();
  return 1;
}


int lib_bus::write_request(uint8_t ident) {
  uint8_t addrbyte = (ident & 0x3f) | addrParity(ident);

  if (kbps == BAUD_19200) {
    Serial2.begin(BAUD_19200_CONST);  // Configure serial baudrate so that writing a 0x00 is the correct break length
  } else Serial2.begin(BAUD_9600_CONST);

  Serial2.write(0x0);  // Write break
  Serial2.flush();

  Serial2.begin(kbps);      // Configure baudrate
  Serial2.write(0x55);      // write Synch Byte to serial
  Serial2.write(addrbyte);  // write Identification Byte to serial

  Serial2.flush();  // Wait untill all data has transmitted
  //Serial2.clear();  // Clear rx buffer
  return 1;
}

int lib_bus::read_request(uint8_t data[], uint8_t data_size) {
  uint8_t i = 0;
  uint8_t rx;


  while (i < data_size) {
    if (Serial2.available()) {
      rx = Serial2.read();
      data[i] = rx;
      Serial.println(rx, HEX);
      i++;
    }
  }

  return 1;
}

#define BIT(data, shift) ((addr & (1 << shift)) >> shift)
uint8_t lib_bus::addrParity(uint8_t addr) {
  uint8_t p0 = BIT(addr, 0) ^ BIT(addr, 1) ^ BIT(addr, 2) ^ BIT(addr, 4);
  uint8_t p1 = ~(BIT(addr, 1) ^ BIT(addr, 3) ^ BIT(addr, 4) ^ BIT(addr, 5));
  return (p0 | (p1 << 1)) << 6;
}

uint8_t lib_bus::dataChecksum(const uint8_t* message, uint8_t nBytes, uint16_t sum) {

  /*while (nBytes-- > 0) sum += *(message++);
  // Add the carry
  while (sum >> 8)  // In case adding the carry causes another carry
    sum = (sum & 255) + (sum >> 8);
  return (~sum);
  */

  for (int i = 0; i < nBytes; i++) {
    sum += message[i];

    if (sum >= 256)
      sum -= 255;
  }
  return (~sum);
}
