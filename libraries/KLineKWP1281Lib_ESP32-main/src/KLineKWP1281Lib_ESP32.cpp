#include "KLineKWP1281Lib_ESP32.h"

/**
  Function:
    KLineKWP1281Lib(beginFunction, endFunction, sendFunction, receiveFunction, tx_pin, full_duplex)

  Parameters:
    beginFunction   -> [void (unsigned long baud)                        ] function to begin communication on the serial port
    endFunction     -> [void ()                                          ] function to end communication on the serial port
    sendFunction    -> [void (uint8_t data)                              ] function to write a byte to the serial port
    receiveFunction -> [bool (uint8_t *data, unsigned long timeout_ticks)] function to receive a byte from the serial port
    tx_pin          -> digital pin, coincides with the selected serial port's transmit pin
    full_duplex     -> flag indicating whether or not the selected serial interface can also read while sending

  Default parameters:
    full_duplex = true

  Description:
    Creates an instance of the library.

  Notes:
    *The return type and parameters taken by each callback function are explained above in brackets.
    *The receiveFunction must return false if no byte is available within the FreeRTOS ticks provided by timeout_ticks. If a byte
    is available, it is stored in the byte passed as pointer and the function returns true.
    *If the serial interface is not able to receive while sending full_duplex should be set to false.
    *If, for some reason, the K-line does not echo back the bytes received, even if the serial interface is capable of receiving
    while sending, full_duplex should be set to false.
*/
KLineKWP1281Lib::KLineKWP1281Lib(beginFunction_type beginFunction, endFunction_type endFunction, sendFunction_type sendFunction, receiveFunction_type receiveFunction, uint8_t tx_pin, bool full_duplex) :
  _beginFunction(beginFunction),
  _endFunction(endFunction),
  _sendFunction(sendFunction),
  _receiveFunction(receiveFunction),
  _tx_pin(tx_pin),
  _full_duplex(full_duplex)
{}

/**
  Function:
    KWP1281debugFunction(KWP1281debugFunction_type debug_function)

  Parameters:
    debug_function -> function to execute after sending/receiving a KWP1281 message, to show it

  Description:
    Attaches a function to be called for debugging KWP1281 messages.

  Notes:
    *The function given here must return void and take the following parameters in the following order:
      - bool direction           -> true for inbound frames (received), false for outbound (sent)
      - uint8_t message_sequence -> message sequence number
      - uint8_t message_type     -> message type/command
      - uint8_t *data            -> message parameters (uint8_t pointer = byte array)
      - size_t length            -> amount of message parameters (amount of bytes in the data array)

    *For example, the function may be defined as such:
      ||void KWP1281debugFunction(bool direction, uint8_t message_sequence, uint8_t message_type, uint8_t *data, size_t length)
      ||{
      ||  printf("\t%s S:%02X T:%02X L:%d ", (direction ? "RECEIVE" : "SEND"), message_sequence, message_type, length);
      ||
      ||  if (length)
      ||  {
      ||    printf("D:");
      ||
      ||    // Iterate through the bytes of the message.
      ||    for (size_t i = 0; i < length; i++)
      ||    {
      ||      printf("%02X ", data[i]);
      ||    }
      ||  }
      ||
      ||  printf("\n");
      ||}

    *Then, it will be attached like:
      ||instanceName.KWP1281debugFunction(KWP1281debugFunction);
*/
void KLineKWP1281Lib::KWP1281debugFunction(KWP1281debugFunction_type debug_function)
{
  _debugFunction = debug_function;
}

/**
  Function:
    customErrorFunction(customErrorFunction_type function)
  
  Parameters:
    function -> [void (uint8_t module, unsigned long baud)] function called when the connection is terminated because of an error

  Description:
    Attaches a custom function to be executed if an error occurs.

  Notes:
    *The previous parameters (module ID and baud rate) are provided, so the application can reconnect to the module on which the error occurred.
    *It will be called once, after a timeout occurs while waiting for data.
    *If no custom function is defined, the library will attempt to reconnect to the previously connected module.
*/
void KLineKWP1281Lib::customErrorFunction(customErrorFunction_type function)
{
  _errorFunction = function;
}

/**
  Function:
    attemptConnect(uint8_t module, unsigned long baud_rate, bool request_extra_identification)

  Parameters:
    module                       -> logical ID of the target module (engine-0x01, cluster-0x17 etc.)
    baud_rate                    -> communication speed (10400, 9600, 4800 etc.)
    request_extra_identification -> whether or not to request extra identification if it is available

  Default parameters:
    request_extra_identification = true

  Returns:
    executionStatus -> whether or not the operation executed successfully
       SUCCESS
       FAIL
       ERROR

  Description:
    Attempts to connect to a module.

  Notes:
    *The module's baud rate must be known beforehand. Trying to connect at an incorrect speed will always fail.
    *If a blocking delay of one second (if the connection fails) is not a problem, use connect(), which will run until successful.
    *If the module contains "extra identification", and request_extra_identification is set to false, that identification string
    will not be requested. The getExtraIdentification() function will return an empty string, but connection will be almost
    1 second faster. So, if you are not interested in the extra identification string, always set this parameter false.
*/
KLineKWP1281Lib::executionStatus KLineKWP1281Lib::attemptConnect(uint8_t module, unsigned long baud_rate, bool request_extra_identification)
{
  // Disable automatically reconnecting upon error, or calling the custom error function.
  error_function_allowed = false;
  
  // If there is a module connected, disconnect from it first.
  if (_current_module)
  {
    disconnect();
  }

  // Wake the target module up.
  K_LOG_INFO("Connecting to module %02X", module);
  bitbang_5baud(module);

  // Initialize the serial port at the requested speed.
  _beginFunction(baud_rate);

  // If initialization is successful, the module will send exactly 3 bytes (0x55 = sync, then 0x018A/0x010A = KWP1281 designator).

  // Try to receive the sync byte without sending a complement, or exit if receiving times out.
  if (!read_byte(&receive_byte, initResponseTimeout, false))
  {
    K_LOG_ERROR("Error reading SYNC byte; check interface/baud rate");
    return ERROR;
  }

  // If the sync byte was received, try to receive the remaining two bytes of the protocol parameters.
  if (receive_byte == 0x55)
  {
    K_LOG_INFO("Got SYNC byte");

    // Try to read the two bytes of protocol identification, or exit if it fails (times out).
    if (!receive_keywords(keyword_buffer))
    {
      K_LOG_ERROR("Error reading KW1+KW2; check interface/baud rate");
      return ERROR;
    }

    // Send a complement to the last byte after a delay.
    vTaskDelay(pdMS_TO_TICKS(initComplementDelay));
    send_complement(keyword_buffer[1]);
    K_LOG_INFO("Connected to module %02X", module);

    // Determine the protocol.
    uint16_t protocol = ((keyword_buffer[1] & 0x7F) << 7) | keyword_buffer[0];
    K_LOG_INFO("Protocol: %d", protocol);

    // Show a warning if the protocol isn't KWP1281.
    if (protocol != 1281)
    {
      K_LOG_WARNING("Protocol may not be supported");
    }

    // Save the current parameters for reconnecting in case of an error.
    _current_baud_rate = baud_rate;
    _current_module = module;

    // After initialization, before sending its identification strings (part number etc.), the module might send these protocol bytes again.
    // This special case will be handled in the first receive_message() function called by read_identification().
    may_send_protocol_parameters_again = true;

    // (Try to) receive the module's identification and populate the private struct with it.
    executionStatus status = read_identification(request_extra_identification);

    // Enable automatically reconnecting upon error, or calling the custom error function.
    error_function_allowed = true;

    // Return what read_identification() returned.
    return status;
  }
  // If something other than the sync byte was received, the baud rate may be incorrect.
  else
  {
    K_LOG_ERROR("Incorrect SYNC byte; check baud rate");
    return ERROR;
  }
}

/**
  Function:
    connect(uint8_t module, unsigned long baud_rate, bool request_extra_identification)

  Parameters:
    module                       -> logical ID of the target module (engine-0x01, cluster-0x17 etc.)
    baud_rate                    -> communication speed (10400, 9600, 4800 etc.)
    request_extra_identification -> whether or not to request extra identification if it is available

  Default parameters:
    request_extra_identification = true

  Description:
    Connects to a module.

  Notes:
    *If establishing the connection fails, this function waits one second before retrying (blocking).
    *To avoid the blocking delay, use attemptConnect() and check its return value in order to execute custom code in case of failure.
    *If the connection cannot be established (i.e. the target module is unavailable), it will run indefinitely.
    *If the module contains "extra identification", and request_extra_identification is set to false, that identification string
    will not be requested. The getExtraIdentification() function will return an empty string, but connection will be almost
    1 second faster. So, if you are not interested in the extra identification string, always set this parameter false.
*/
void KLineKWP1281Lib::connect(uint8_t module, unsigned long baud_rate, bool request_extra_identification)
{
  while (attemptConnect(module, baud_rate, request_extra_identification) != SUCCESS)
  {
    // If connection fails, wait one second before retrying.
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

/**
  Function:
    update()

  Description:
    Maintains the connection to the control module.

  Notes:
    *After ~500ms of inactivity without update(), the connection breaks.
*/
void KLineKWP1281Lib::update()
{
  // To maintain the connection, send an acknowledgement, to which the module also sends an acknowledgement.
  if (!acknowledge())
  {
    show_debug_info(SEND_ERROR);
  }

  // Check the response.
  size_t bytes_received;
  switch (receive_message(&bytes_received, receive_buffer, sizeof(receive_buffer), responseTimeout))
  {
  case TYPE_ACK:
    break;

  default:
    show_debug_info(UNEXPECTED_RESPONSE);
    break;
  }
}

/**
  Function:
    disconnect(bool wait_for_response)

  Parameters:
    wait_for_response -> whether or not to wait for a response

  Default parameters:
    wait_for_response = true

  Description:
    Disconnects from the control module.

  Notes:
    *Some modules send a response to the disconnect request, some don't.
    *Setting wait_for_response to false will save responseTimeout milliseconds if the module doesn't respond to the disconnect request.
*/
void KLineKWP1281Lib::disconnect(bool wait_for_response)
{
  // Request to disconnect.
  send_message(KWP_DISCONNECT);

  // Indicate that there is no module connected anymore, so the following read will not trigger the error function if it times out.
  _current_module = 0;

  // Some modules may send a "refuse" block, don't leave it un-complemented.
  // Don't wait for a response if not requested.
  if (wait_for_response)
  {
    show_debug_info(DISCONNECT_INFO);
    size_t bytes_received;
    receive_message(&bytes_received, receive_buffer, sizeof(receive_buffer), responseTimeout);
  }

  // Clear the strings in the module identification struct.
  memset(identification_data.part_number, 0, sizeof(identification_data.part_number));
  memset(identification_data.identification, 0, sizeof(identification_data.identification));
  memset(identification_data.extra_identification, 0, sizeof(identification_data.extra_identification));
}

/**
  Function:
    getPartNumber()

  Returns:
    char* -> pointer to a character array

  Description:
    Provides a string containing the module's VAG part number.
*/
char *KLineKWP1281Lib::getPartNumber()
{
  return identification_data.part_number;
}

/**
  Function:
    getIdentification()

  Returns:
    char* -> pointer to a character array

  Description:
    Provides a string containing the module's model name.
*/
char *KLineKWP1281Lib::getIdentification()
{
  return identification_data.identification;
}

/**
  Function:
    getExtraIdentification()

  Returns:
    char* -> pointer to a character array

  Description:
    Provides a string containing the module's extra identification (such as a VIN).

  Notes:
    *If the module does not have any extra identification, the string will have a null size.
*/
char *KLineKWP1281Lib::getExtraIdentification()
{
  return identification_data.extra_identification;
}

/**
  Function:
    getCoding()

  Returns:
    uint16_t -> the coding value

  Description:
    Provides the module's coding value.
*/
uint16_t KLineKWP1281Lib::getCoding()
{
  return identification_data.coding;
}

/**
  Function:
    getWorkshopCode()

  Returns:
    uint16_t -> the workshop code

  Description:
    Provides the module's WSC.
*/
uint32_t KLineKWP1281Lib::getWorkshopCode()
{
  return identification_data.workshop_code;
}

/**
  Function:
    login(uint16_t login_code, uint32_t workshop_code)

  Parameters:
    login_code    -> the login code
    workshop_code -> the CURRENT workshop code

  Returns:
    executionStatus -> whether or not the operation executed successfully
       SUCCESS
       FAIL
       ERROR

  Description:
    Performs a login operation.
*/
KLineKWP1281Lib::executionStatus KLineKWP1281Lib::login(uint16_t login_code, uint32_t workshop_code)
{
  // Separate the login code into 2 bytes.
  uint8_t login_code_high_byte = login_code >> 8;
  uint8_t login_code_low_byte = login_code & 0xFF;

  // Separate the workshop code into 3 bytes.
  uint8_t workshop_code_top_bit = (workshop_code >> 16) & 0x01;
  uint8_t workshop_code_high_byte = (workshop_code >> 8) & 0xFF;
  uint8_t workshop_code_low_byte = workshop_code & 0xFF;

  // Send the Login request with the login code and workshop code as parameters.
  uint8_t parameters[] = {login_code_high_byte, login_code_low_byte, workshop_code_top_bit, workshop_code_high_byte, workshop_code_low_byte};
  if (!send_message(KWP_REQUEST_LOGIN, parameters, sizeof(parameters)))
  {
    show_debug_info(SEND_ERROR);
    return ERROR;
  }

  // Check the response.
  size_t bytes_received;
  switch (receive_message(&bytes_received, receive_buffer, sizeof(receive_buffer), responseTimeout))
  {
  case TYPE_REFUSE:
    show_debug_info(LOGIN_NOT_ACCEPTED);
    return FAIL;

  case TYPE_ACK:
    show_debug_info(LOGIN_ACCEPTED);
    return SUCCESS;

  default:
    show_debug_info(UNEXPECTED_RESPONSE);
    return ERROR;
  }
}

/**
  Function:
    recode(uint16_t coding, uint32_t workshop_code)

  Parameters:
    coding        -> the desired coding
    workshop_code -> the desired workshop code

  Default parameters:
    workshop_code = 0 (00000)

  Returns:
    executionStatus -> whether or not the operation executed successfully
       SUCCESS
       FAIL
       ERROR

  Description:
    Recodes the module.

  Notes:
    *The new workshop code may only be applied if currently logged in with the previous code.
*/
KLineKWP1281Lib::executionStatus KLineKWP1281Lib::recode(uint16_t coding, uint32_t workshop_code)
{
  // The coding value is shifted left by 1.
  coding <<= 1;

  // Separate/combine the coding and worksop code into 4 bytes.
  uint8_t coding_high_byte = coding >> 8;
  uint8_t coding_low_byte_workshop_code_top_bit = (coding & 0xFF) | ((workshop_code >> 16) & 0x01);
  uint8_t workshop_code_high_byte = (workshop_code >> 8) & 0xFF;
  uint8_t workshop_code_low_byte = workshop_code & 0xFF;

  // Send the Recode request with the coding and workshop code as parameters.
  uint8_t parameters[] = {coding_high_byte, coding_low_byte_workshop_code_top_bit, workshop_code_high_byte, workshop_code_low_byte};
  if (!send_message(KWP_REQUEST_RECODE, parameters, sizeof(parameters)))
  {
    show_debug_info(SEND_ERROR);
    return ERROR;
  }

  // After recoding, the module automatically sends its identification like when initializing.
  return read_identification(false);
}

/**
  Function:
    readFaults(uint8_t &amount_of_fault_codes, uint8_t fault_code_buffer[], size_t fault_code_buffer_size)

  Parameters:
    amount_of_fault_codes  -> will contain the total number of fault codes
    fault_code_buffer      -> array into which to store the fault codes
    fault_code_buffer_size -> total (maximum) length of the given array

  Default parameters:
    fault_code_buffer = {}
    fault_code_buffer_size = 0

  Returns:
    executionStatus -> whether or not the operation executed successfully
       SUCCESS
       FAIL
       ERROR

  Description:
    Reads the module's fault codes.

  Notes:
    *The DTCs are stored in the following order: DTC_H, DTC_L, DTC_ELABORATION.
    *The functions getFaultCode() and getFaultElaborationCode() can be used to easily get the fault+elaboration codes stored in the buffer by readFaults().
    *getFaultCode() and getFaultElaborationCode() are static functions so they do not require an instance to be used.
    *Some fault codes may be sent in a "standard OBD" format. Determine this using the isOBDFaultCode() function.
    *If the fault is indeed OBD, you can get a string containing the correct representation with getOBDFaultCode().
*/
KLineKWP1281Lib::executionStatus KLineKWP1281Lib::readFaults(uint8_t &amount_of_fault_codes, uint8_t *fault_code_buffer, size_t fault_code_buffer_size)
{
  // Request the fault codes.
  if (!send_message(KWP_REQUEST_FAULT_CODES))
  {
    show_debug_info(SEND_ERROR);
    return ERROR;
  }

  uint8_t faults = 0;                                            // will contain the total amount of fault codes
  uint8_t fault_buffer_index = 0;                                // for saving fault codes in the array
  uint8_t fault_codes_in_current_block;                          // will contain the amount of fault codes in the currently received message
  uint8_t max_fault_codes_in_array = fault_code_buffer_size / 3; // how many fault codes would fit in the array

  /*
     One response normally contains at most 4 fault codes.
     If more than 4 codes are stored, multiple responses will be received.
     An "acknowledgement" message marks the end of the fault codes response.
  */

  // Loop until no more fault codes can be read.
  while (true)
  {
    size_t bytes_received;
    switch (receive_message(&bytes_received, receive_buffer, sizeof(receive_buffer), responseTimeout))
    {
    case TYPE_REFUSE:
      show_debug_info(FAULT_CODES_NOT_SUPPORTED);
      return FAIL;

    case TYPE_ACK:
      // Store how many DTCs have been read after receiving the last block.
      amount_of_fault_codes = faults;
      return SUCCESS;

    case TYPE_FAULT_CODES:
    {
      // Determine how many fault codes have just been received (a fault code takes 3 bytes).
      fault_codes_in_current_block = bytes_received / 3;

      // A single DTC, with code 0xFFFF and elaboration 0x88, means no DTCs present.
      if (fault_codes_in_current_block == 1)
      {
        if (receive_buffer[0] == 0xFF && receive_buffer[1] == 0xFF)
        {
          if (receive_buffer[2] == 0x88)
          {
            amount_of_fault_codes = 0;
            return SUCCESS;
          }
        }
      }

      // If fault codes were received, increment the counter.
      faults += fault_codes_in_current_block;

      // Issue a debug warning if the data will not fit in the buffer.
      if (fault_codes_in_current_block > max_fault_codes_in_array)
      {
        show_debug_info(ARRAY_NOT_LARGE_ENOUGH);
      }

      // Calculate how many fault codes will be copied in the given array (how many will fit).
      uint8_t fault_codes_to_copy = min(fault_codes_in_current_block, max_fault_codes_in_array);

      // Each code takes up 3 bytes.
      memcpy(fault_code_buffer + fault_buffer_index, receive_buffer, fault_codes_to_copy * 3);

      // Increment the buffer index by how many bytes were copied.
      fault_buffer_index += fault_codes_to_copy * 3;

      // Decrement the available space in the array.
      max_fault_codes_in_array -= fault_codes_to_copy;

      // Confirm receipt and request the next response.
      if (!acknowledge())
      {
        show_debug_info(SEND_ERROR);
        return ERROR;
      }
      break;
    }

    default:
      show_debug_info(UNEXPECTED_RESPONSE);
      return ERROR;
    }
  }
}

/**
  Function:
    getFaultCode(uint8_t fault_code_index, uint8_t amount_of_fault_codes, uint8_t fault_code_buffer[], size_t fault_code_buffer_size)

  Parameters:
    fault_code_index       -> index of the fault code that needs to be retrieved
    amount_of_fault_codes  -> total number of fault codes stored in the array (value passed as reference to readFaults())
    fault_code_buffer      -> array in which fault codes have been stored by readFaults()
    fault_code_buffer_size -> total size of the given array (provided with the sizeof() operator)

  Returns:
    uint16_t -> fault code

  Description:
    Provides a fault code from a buffer filled by readFaults().

  Notes:
    *It is a static function, so it does not require an instance to be used.
*/
uint16_t KLineKWP1281Lib::getFaultCode(uint8_t fault_code_index, uint8_t amount_of_fault_codes, uint8_t *fault_code_buffer, size_t fault_code_buffer_size)
{
  // Check if the array is large enough to contain as many fault codes as "declared" by the value given.
  if (fault_code_buffer_size < amount_of_fault_codes * 3)
  {
    amount_of_fault_codes = fault_code_buffer_size / 3;
  }

  // If the requested index is out of range (or the buffer doesn't contain fault codes), return 0.
  if (fault_code_index >= amount_of_fault_codes)
  {
    return 0;
  }

  // Combine the two bytes to form the code and return it.
  uint16_t fault_code = (fault_code_buffer[3 * fault_code_index] << 8) | fault_code_buffer[3 * fault_code_index + 1];
  return fault_code;
}

/**
  Function:
    isOBDFaultCode(uint8_t fault_code_index, uint8_t amount_of_fault_codes, uint8_t fault_code_buffer[], size_t fault_code_buffer_size)
    isOBDFaultCode(uint16_t fault_code)

  Parameters (1):
    fault_code_index       -> index of the fault code whose type needs to be determined
    amount_of_fault_codes  -> total number of fault codes stored in the array (value passed as reference to readFaults())
    fault_code_buffer      -> array in which fault codes have been stored by readFaults()
    fault_code_buffer_size -> total size of the given array (provided with the sizeof() operator)

  Parameters (2):
    fault_code  -> fault code given by getFaultCode()

  Returns:
    bool -> whether or not the fault code is of standard OBD type

  Description:
    Determines whether or not a fault is of the "standard OBD" type.

  Notes:
    *If a fault code is of standard OBD type, use getOBDFaultCode() to get a string containing the formatted code.
    *It is a static function, so it does not require an instance to be used.
*/
bool KLineKWP1281Lib::isOBDFaultCode(uint8_t fault_code_index, uint8_t amount_of_fault_codes, uint8_t *fault_code_buffer, size_t fault_code_buffer_size)
{
  // Determine the fault code.
  uint16_t fault_code = getFaultCode(fault_code_index, amount_of_fault_codes, fault_code_buffer, fault_code_buffer_size);

  // Use the other function.
  return isOBDFaultCode(fault_code);
}

bool KLineKWP1281Lib::isOBDFaultCode(uint16_t fault_code)
{
  // The fault code is of standard OBD type if it's between 0x4000 and 0x7FFF.
  return (fault_code >= 0x4000 && fault_code <= 0x7FFF);
}

/**
  Function:
    getOBDFaultCode(uint8_t fault_code_index, uint8_t amount_of_fault_codes, uint8_t fault_code_buffer[], size_t fault_code_buffer_size, char str[], size_t string_size)
    getOBDFaultCode(uint16_t fault_code, char str[], size_t string_size)

  Parameters (1):
    fault_code_index       -> index of the fault code whose formatted code needs to be retrieved
    amount_of_fault_codes  -> total number of fault codes stored in the array (value passed as reference to readFaults())
    fault_code_buffer      -> array in which fault codes have been stored by readFaults()
    fault_code_buffer_size -> total size of the given array (provided with the sizeof() operator)
    str                    -> string (character array) into which to copy the description string
    string_size            -> total size of the given array (provided with the sizeof() operator)

  Parameters (2):
    fault_code  -> fault code given by getFaultCode()
    str         -> string (character array) into which to copy the description string
    string_size -> total size of the given array (provided with the sizeof() operator)

  Returns:
    char* -> the same character array provided (str)

  Description:
    Formats a standard OBD fault code.

  Notes:
    *If the provided fault code is not of the standard OBD type, an empty string is returned.
    *It is a static function, so it does not require an instance to be used.
*/
char *KLineKWP1281Lib::getOBDFaultCode(uint8_t fault_code_index, uint8_t amount_of_fault_codes, uint8_t *fault_code_buffer, size_t fault_code_buffer_size, char *str, size_t string_size)
{
  // Determine the fault code.
  uint16_t fault_code = getFaultCode(fault_code_index, amount_of_fault_codes, fault_code_buffer, fault_code_buffer_size);

  // Use the other function.
  return getOBDFaultCode(fault_code, str, string_size);
}

char *KLineKWP1281Lib::getOBDFaultCode(uint16_t fault_code, char *str, size_t string_size)
{
  // If an invalid buffer was provided, return an invalid string.
  if (!str || !string_size)
  {
    return nullptr;
  }

  // If the provided fault code is not of the standard OBD type, return an empty string.
  if (!isOBDFaultCode(fault_code))
  {
    str[0] = '\0';
    return str;
  }

  // Determine the fault's category (P/C/B/U).
  char category_type = 0;
  switch ((fault_code >> 0xC) & 0xF)
  {
  case 4:
    category_type = 'P';
    break;
  case 5:
    category_type = 'C';
    break;
  case 6:
    category_type = 'B';
    break;
  case 7:
    category_type = 'U';
    break;
  }

  // Determine the fault's category code (for example, for P1 it gets 1).
  uint8_t category_code = ((fault_code >> 8) & 0xF) / 4;

  // Determine the last 3 digits of the code.
  uint16_t converted_dtc = fault_code - (((((fault_code >> 0xC) & 0xF) << 4) | (category_code << 2)) << 8);

  // If the last 3 digits somehow went above 999, return an empty string.
  if (converted_dtc > 999)
  {
    str[0] = '\0';
    return str;
  }

  // Format the string into the given buffer.
  snprintf(str, string_size, "%c%d%03d", category_type, category_code, converted_dtc);
  str[string_size - 1] = '\0';
  return str;
}

/**
  Function:
    getFaultDescription(uint8_t fault_code_index, uint8_t amount_of_fault_codes, uint8_t fault_code_buffer[], size_t fault_code_buffer_size, char str[], size_t string_size)
    getFaultDescription(uint16_t fault_code, char str[], size_t string_size)

  Parameters (1):
    fault_code_index       -> index of the fault code whose description needs to be retrieved
    amount_of_fault_codes  -> total number of fault codes stored in the array (value passed as reference to readFaults())
    fault_code_buffer      -> array in which fault codes have been stored by readFaults()
    fault_code_buffer_size -> total size of the given array (provided with the sizeof() operator)
    str                    -> string (character array) into which to copy the description string
    string_size            -> total size of the given array (provided with the sizeof() operator)

  Parameters (2):
    fault_code  -> fault code given by getFaultCode()
    str         -> string (character array) into which to copy the description string
    string_size -> total size of the given array (provided with the sizeof() operator)

  Returns:
    char* -> the same character array provided (str)

  Description:
    Provides a fault description string from a buffer filled by readFaults().

  Notes:
    *If the fault code is of the standard OBD type, the description may also contain the fault elaboration string at the end,
    after the ':' character.
    *It is a static function, so it does not require an instance to be used.
*/
char *KLineKWP1281Lib::getFaultDescription(uint8_t fault_code_index, uint8_t amount_of_fault_codes, uint8_t *fault_code_buffer, size_t fault_code_buffer_size, char *str, size_t string_size)
{
  // Determine the fault code.
  uint16_t fault_code = getFaultCode(fault_code_index, amount_of_fault_codes, fault_code_buffer, fault_code_buffer_size);

  // Use the other function.
  return getFaultDescription(fault_code, str, string_size);
}

char *KLineKWP1281Lib::getFaultDescription(uint16_t fault_code, char *str, size_t string_size)
{
  // If an invalid buffer was provided, return an invalid string.
  if (!str || !string_size)
  {
    return nullptr;
  }

  // Determine the fault's type.
  if (isOBDFaultCode(fault_code))
  {
    // If the feature is enabled, copy the description into the given string.
#ifdef KWP1281_OBD_FAULT_CODE_DESCRIPTION_SUPPORTED

    // Get the code formatted correctly.
    char OBD_fault_code_string[6];
    getOBDFaultCode(fault_code, OBD_fault_code_string, sizeof(OBD_fault_code_string));

    // Calculate the text code of the description string, by converting the code to BCD and adding the fault's category.
    unsigned int text_code;
    sscanf(&OBD_fault_code_string[1], "%04x", &text_code);
    text_code |= (((fault_code >> 0xC) & 0xF) - 4) << 0xE;

    // The calculated text code will be used as the key for a binary search.
    struct keyed_struct bsearch_key;
    bsearch_key.code = text_code;

    // Binary search the key in the OBD fault description table.
    struct keyed_struct *result = (struct keyed_struct *)bsearch(
        &bsearch_key, OBD_fault_description_table_entries, ARRAYSIZE(OBD_fault_description_table_entries),
        sizeof(struct keyed_struct), compare_keyed_structs);

    // If the text is found, copy it into the string.
    if (result)
    {
      // Copy the description string.
      strncpy(str, result->text, string_size);
      str[string_size - 1] = '\0';
    }
    // Otherwise, clear the string.
    else
    {
      str[0] = '\0';
    }

#else

    // Otherwise, copy the "warning" into the given string.
    strncpy(str, "EN_obd", string_size);
    str[string_size - 1] = '\0';

#endif
  }
  else
  {
    // If the feature is enabled, copy the description into the given string.
#ifdef KWP1281_FAULT_CODE_DESCRIPTION_SUPPORTED

    // The fault code will be used as the key for a binary search.
    struct keyed_struct bsearch_key;
    bsearch_key.code = fault_code;

    // Binary search the key in the regular fault description table.
    struct keyed_struct *result = (struct keyed_struct *)bsearch(
        &bsearch_key, fault_description_table_entries, ARRAYSIZE(fault_description_table_entries),
        sizeof(struct keyed_struct), compare_keyed_structs);

    // If the text is found, copy it into the string.
    if (result)
    {
      // Copy the description string.
      strncpy(str, result->text, string_size);
      str[string_size - 1] = '\0';
    }
    // Otherwise, clear the string.
    else
    {
      str[0] = '\0';
    }

#else

    // Otherwise, copy the "warning" into the given string.
    strncpy(str, "EN_dsc", string_size);
    str[string_size - 1] = '\0';

#endif
  }

  // Return a pointer to the given array.
  return str;
}

/**
  Function:
    getFaultDescriptionLength(uint8_t fault_code_index, uint8_t amount_of_fault_codes, uint8_t fault_code_buffer[], size_t fault_code_buffer_size)
    getFaultDescriptionLength(uint16_t fault_code)

  Parameters (1):
    fault_code_index       -> index of the fault code whose description needs to be retrieved
    amount_of_fault_codes  -> total number of fault codes stored in the array (value passed as reference to readFaults())
    fault_code_buffer      -> array in which fault codes have been stored by readFaults()
    fault_code_buffer_size -> total size of the given array (provided with the sizeof() operator)

  Parameters (2):
    fault_code  -> fault code given by getFaultCode()

  Returns:
    size_t -> string length

  Description:
    Provides the length of the fault description string from a buffer filled by readFaults().

  Notes:
    *It is a static function, so it does not require an instance to be used.
*/
size_t KLineKWP1281Lib::getFaultDescriptionLength(uint8_t fault_code_index, uint8_t amount_of_fault_codes, uint8_t *fault_code_buffer, size_t fault_code_buffer_size)
{
  // Determine the fault code.
  uint16_t fault_code = getFaultCode(fault_code_index, amount_of_fault_codes, fault_code_buffer, fault_code_buffer_size);

  // Use the other function.
  return getFaultDescriptionLength(fault_code);
}

size_t KLineKWP1281Lib::getFaultDescriptionLength(uint16_t fault_code)
{
  // If the feature is enabled, get the string length.
#ifdef KWP1281_FAULT_CODE_DESCRIPTION_SUPPORTED

  // The fault code will be used as the key for a binary search.
  struct keyed_struct bsearch_key;
  bsearch_key.code = fault_code;

  // Binary search the key in the fault description table.
  struct keyed_struct *result = (struct keyed_struct *)bsearch(
      &bsearch_key, fault_description_table_entries, ARRAYSIZE(fault_description_table_entries),
      sizeof(struct keyed_struct), compare_keyed_structs);

  // If the text is found, return its length.
  if (result)
  {
    return strlen(result->text);
  }
  // Otherwise, return 0.
  else
  {
    return 0;
  }

#else

  // Mark the parameter as unused.
  (void)fault_code;

  // If the feature is not enabled, return the length of the "warning";
  return strlen("EN_dsc");

#endif

  // If the function got here, an error occurred, so return 0.
  return 0;
}

/**
  Function:
    getFaultElaborationCode(uint8_t fault_code_index, uint8_t amount_of_fault_codes, uint8_t fault_code_buffer[], size_t fault_code_buffer_size)

  Parameters:
    fault_code_index       -> index of the fault code whose elaboration code needs to be retrieved
    amount_of_fault_codes  -> total number of fault codes stored in the array (value passed as reference to readFaults())
    fault_code_buffer      -> array in which fault codes have been stored by readFaults()
    fault_code_buffer_size -> total size of the given array (provided with the sizeof() operator)

  Returns:
    uint8_t -> fault elaboration code

  Description:
    Provides a fault elaboration code from a buffer filled by readFaults().

  Notes:
    *It is a static function, so it does not require an instance to be used.
*/
uint8_t KLineKWP1281Lib::getFaultElaborationCode(uint8_t fault_code_index, uint8_t amount_of_fault_codes, uint8_t *fault_code_buffer, size_t fault_code_buffer_size)
{
  // Check if the array is large enough to contain as many fault codes as "declared" by the value given.
  if (fault_code_buffer_size < amount_of_fault_codes * 3)
  {
    amount_of_fault_codes = fault_code_buffer_size / 3;
  }

  // If the requested index is out of range (or the buffer doesn't contain fault codes), return 0.
  if (fault_code_index >= amount_of_fault_codes)
  {
    return 0;
  }

  // Return the elaboration code.
  return fault_code_buffer[3 * fault_code_index + 2];
}

/**
  Function:
    getFaultElaboration(bool &is_intermittent, uint8_t fault_code_index, uint8_t amount_of_fault_codes, uint8_t fault_code_buffer[], size_t fault_code_buffer_size, char str[], size_t string_size)
    getFaultElaboration(bool &is_intermittent, uint8_t elaboration_code, char str[], size_t string_size)

  Parameters (1):
    is_intermittent        -> whether or not the elaboration code indicated that the fault is "Intermittent"
    fault_code_index       -> index of the fault code whose elaboration needs to be retrieved
    amount_of_fault_codes  -> total number of fault codes stored in the array (value passed as reference to readFaults())
    fault_code_buffer      -> array in which fault codes have been stored by readFaults()
    fault_code_buffer_size -> total size of the given array (provided with the sizeof() operator)
    str                    -> string (character array) into which to copy the elaboration string
    string_size            -> total size of the given array (provided with the sizeof() operator)

  Parameters (2):
    is_intermittent  -> whether or not the elaboration code indicated that the fault is "Intermittent"
    elaboration_code -> elaboration code given by getFaultElaborationCode()
    str              -> string (character array) into which to copy the elaboration string
    string_size      -> total size of the given array (provided with the sizeof() operator)

  Returns:
    char* -> the same character array provided (str)

  Description:
    Provides a fault elaboration string from a buffer filled by readFaults().

  Notes:
    *The fault elaboration may not be correct if the fault code is of standard OBD type.
    *Use the isOBDFaultCode() function to check the type of fault code.
    *If it is of standard OBD type, the elaboration will be contained in the description string,
    after the ':' character.
    *It is a static function, so it does not require an instance to be used.
*/
char *KLineKWP1281Lib::getFaultElaboration(bool &is_intermittent, uint8_t fault_code_index, uint8_t amount_of_fault_codes, uint8_t *fault_code_buffer, size_t fault_code_buffer_size, char *str, size_t string_size)
{
  // Determine the elaboration code.
  uint8_t elaboration_code = getFaultElaborationCode(fault_code_index, amount_of_fault_codes, fault_code_buffer, fault_code_buffer_size);

  // Use the other function.
  return getFaultElaboration(is_intermittent, elaboration_code, str, string_size);
}

char *KLineKWP1281Lib::getFaultElaboration(bool &is_intermittent, uint8_t elaboration_code, char *str, size_t string_size)
{
  // If an invalid buffer was provided, return an invalid string.
  if (!str || !string_size)
  {
    return nullptr;
  }

  // The fault is considered "Intermittent" if the highest bit in the elaboration code is set.
  is_intermittent = elaboration_code & 0x80;

  // Ensure the high bit is not set.
  elaboration_code &= ~0x80;

  // If the feature is enabled, copy the elaboration into the given string.
#ifdef KWP1281_FAULT_CODE_ELABORATION_SUPPORTED

  // If the elaboration code is valid, copy its string into the given array.
  if (elaboration_code < ARRAYSIZE(fault_elaboration_table))
  {
    // Copy the elaboration string into the given array.
    strncpy(str, fault_elaboration_table[elaboration_code], string_size);

    // Ensure the string is null-terminated.
    if (string_size)
    {
      str[string_size - 1] = '\0';
    }
  }
  // Otherwise, clear the string by writing a null on the first position.
  else
  {
    str[0] = '\0';
  }

#else

  // Otherwise, copy the "warning" into the given string.
  strncpy(str, "EN_elb", string_size);
  str[string_size - 1] = '\0';

#endif

  // Return a pointer to the given array.
  return str;
}

/**
  Function:
    getFaultElaborationLength(uint8_t fault_code_index, uint8_t amount_of_fault_codes, uint8_t fault_code_buffer[], size_t fault_code_buffer_size)
    getFaultElaborationLength(uint8_t elaboration_code)

  Parameters (1):
    fault_code_index       -> index of the fault code whose elaboration length needs to be retrieved
    amount_of_fault_codes  -> total number of fault codes stored in the array (value passed as reference to readFaults())
    fault_code_buffer      -> array in which fault codes have been stored by readFaults()
    fault_code_buffer_size -> total size of the given array (provided with the sizeof() operator)

  Parameters (2):
    elaboration_code -> elaboration code given by getFaultElaborationCode()

  Returns:
    size_t -> string length

  Description:
    Provides the length of the fault elaboration string from a buffer filled by readFaults().

  Notes:
    *It is a static function, so it does not require an instance to be used.
*/
size_t KLineKWP1281Lib::getFaultElaborationLength(uint8_t fault_code_index, uint8_t amount_of_fault_codes, uint8_t *fault_code_buffer, size_t fault_code_buffer_size)
{
  // Determine the elaboration code.
  uint8_t elaboration_code = getFaultElaborationCode(fault_code_index, amount_of_fault_codes, fault_code_buffer, fault_code_buffer_size);

  // Use the other function.
  return getFaultElaborationLength(elaboration_code);
}

size_t KLineKWP1281Lib::getFaultElaborationLength(uint8_t elaboration_code)
{
  // If the feature is enabled, get the string length.
#ifdef KWP1281_FAULT_CODE_ELABORATION_SUPPORTED

  // Ensure the high bit is not set.
  elaboration_code &= ~0x80;

  // If the elaboration code is valid, return its length.
  if (elaboration_code < ARRAYSIZE(fault_elaboration_table))
  {
    return strlen(fault_elaboration_table[elaboration_code]);
  }

#else

  // Mark the parameter as unused.
  (void)elaboration_code;

  // If the feature is not enabled, return the length of the "warning";
  return strlen("EN_elb");

#endif

  // If the function got here, an error occurred, so return 0.
  return 0;
}

/**
  Function:
    clearFaults()

  Returns:
    executionStatus -> whether or not the operation executed successfully
       SUCCESS
       FAIL
       ERROR

  Description:
    Clears the module's diagnostic trouble codes.

  Notes:
    *Run readFaults() afterwards to check if it was effective.
*/
KLineKWP1281Lib::executionStatus KLineKWP1281Lib::clearFaults()
{
  if (!send_message(KWP_REQUEST_CLEAR_FAULTS))
  {
    show_debug_info(SEND_ERROR);
    return ERROR;
  }

  size_t bytes_received;
  switch (receive_message(&bytes_received, receive_buffer, sizeof(receive_buffer), responseTimeout))
  {
  case TYPE_REFUSE:
    show_debug_info(CLEARING_FAULT_CODES_NOT_SUPPORTED);
    return FAIL;

  case TYPE_ACK:
    show_debug_info(CLEARING_FAULT_CODES_ACCEPTED);
    return SUCCESS;

  default:
    show_debug_info(UNEXPECTED_RESPONSE);
    return ERROR;
  }
}

/**
  Function:
    readAdaptation(uint8_t channel, uint16_t &value)

  Parameters:
    channel -> adaptation channel to read
    value   -> will store the value read from the channel

  Returns:
    executionStatus -> whether or not the operation executed successfully
       SUCCESS
       FAIL
       ERROR

  Description:
    Reads the value of an adaptation channel.
*/
KLineKWP1281Lib::executionStatus KLineKWP1281Lib::readAdaptation(uint8_t channel, uint16_t &value)
{
  uint8_t parameters[] = {channel};
  if (!send_message(KWP_REQUEST_ADAPTATION, parameters, sizeof(parameters)))
  {
    show_debug_info(SEND_ERROR);
    return ERROR;
  }

  size_t bytes_received;
  switch (receive_message(&bytes_received, receive_buffer, sizeof(receive_buffer), responseTimeout))
  {
  case TYPE_REFUSE:
    show_debug_info(INVALID_ADAPTATION_CHANNEL);
    return FAIL;

  case TYPE_ADAPTATION:
    show_debug_info(ADAPTATION_RECEIVED);

    // Combine the two bytes to get the adaptation value.
    value = (receive_buffer[1] << 8) | receive_buffer[2];
    return SUCCESS;

  default:
    show_debug_info(UNEXPECTED_RESPONSE);
    return ERROR;
  }
}

/**
  Function:
    testAdaptation(uint8_t channel, uint16_t value)

  Parameters:
    channel -> adaptation channel to test the value on
    value   -> value to test

  Returns:
    executionStatus -> whether or not the operation executed successfully
       SUCCESS
       FAIL
       ERROR

  Description:
    Tests whether or not a value would work for an adaptation channel.
*/
KLineKWP1281Lib::executionStatus KLineKWP1281Lib::testAdaptation(uint8_t channel, uint16_t value)
{
  uint8_t value_high_byte = value >> 8;
  uint8_t value_low_byte = value & 0xFF;

  uint8_t parameters[] = {channel, value_high_byte, value_low_byte};
  if (!send_message(KWP_REQUEST_ADAPTATION_TEST, parameters, sizeof(parameters)))
  {
    show_debug_info(SEND_ERROR);
    return ERROR;
  }

  size_t bytes_received;
  switch (receive_message(&bytes_received, receive_buffer, sizeof(receive_buffer), responseTimeout))
  {
  case TYPE_REFUSE:
    show_debug_info(INVALID_ADAPTATION_CHANNEL);
    return FAIL;

  case TYPE_ADAPTATION:
    if (uint16_t((receive_buffer[1] << 8) | receive_buffer[2]) == value)
    {
      show_debug_info(ADAPTATION_ACCEPTED);
      return SUCCESS;
    }
    else
    {
      show_debug_info(ADAPTATION_NOT_ACCEPTED);
      return FAIL;
    }
    break;

  default:
    show_debug_info(UNEXPECTED_RESPONSE);
    return ERROR;
  }
}

/**
  Function:
    adapt(uint8_t channel, uint16_t value, uint32_t workshop_code)

  Parameters:
    channel       -> adaptation channel to modify
    value         -> value to store in the channel
    workshop_code -> WSC to use

  Returns:
    executionStatus -> whether or not the operation executed successfully
       SUCCESS
       FAIL
       ERROR

  Description:
    Changes the value of an adaptation channel.

  Notes:
    *Use testAdaptation() before adapt(), to ensure it's possible to use the value on the channel.
*/
KLineKWP1281Lib::executionStatus KLineKWP1281Lib::adapt(uint8_t channel, uint16_t value, uint32_t workshop_code)
{
  uint8_t value_high_byte = value >> 8;
  uint8_t value_low_byte = value & 0xFF;

  uint8_t workshop_code_top_bit = (workshop_code >> 16) & 0x01;
  uint8_t workshop_code_high_byte = (workshop_code >> 8) & 0xFF;
  uint8_t workshop_code_low_byte = workshop_code & 0xFF;

  uint8_t parameters[] = {channel, value_high_byte, value_low_byte, workshop_code_top_bit, workshop_code_high_byte, workshop_code_low_byte};
  if (!send_message(KWP_REQUEST_ADAPTATION_SAVE, parameters, sizeof(parameters)))
  {
    show_debug_info(SEND_ERROR);
    return ERROR;
  }

  size_t bytes_received;
  switch (receive_message(&bytes_received, receive_buffer, sizeof(receive_buffer), responseTimeout))
  {
  case TYPE_REFUSE:
    show_debug_info(INVALID_ADAPTATION_CHANNEL_OR_VALUE);
    return FAIL;

  case TYPE_ADAPTATION:
    show_debug_info(ADAPTATION_RECEIVED);
    return SUCCESS;

  default:
    show_debug_info(UNEXPECTED_RESPONSE);
    return ERROR;
  }
}

/**
  Function:
    basicSetting(uint8_t &amount_of_values, uint8_t group, uint8_t basic_setting_buffer[], size_t basic_setting_buffer_size)

  Parameters:
    amount_of_values          -> will contain the total number of values read from the basic setting group
    group                     -> basic setting group to activate/read
    basic_setting_buffer      -> array into which to store the received values
    basic_setting_buffer_size -> total (maximum) length of the given array

  Default parameters:
    basic_setting_buffer = {}
    basic_setting_buffer_size = 0

  Returns:
    executionStatus -> whether or not the operation executed successfully
       SUCCESS
       FAIL
       ERROR

  Description:
    Performs a basic setting.
*/
KLineKWP1281Lib::executionStatus KLineKWP1281Lib::basicSetting(uint8_t &amount_of_values, uint8_t group, uint8_t *basic_setting_buffer, size_t basic_setting_buffer_size)
{
  if (group)
  {
    uint8_t parameters[] = {group};
    if (!send_message(KWP_REQUEST_BASIC_SETTING, parameters, sizeof(parameters)))
    {
      show_debug_info(SEND_ERROR);
      return ERROR;
    }
  }
  else
  {
    if (!send_message(KWP_REQUEST_BASIC_SETTING_0))
    {
      show_debug_info(SEND_ERROR);
      return ERROR;
    }
  }

  size_t bytes_received;
  switch (receive_message(&bytes_received, receive_buffer, sizeof(receive_buffer), responseTimeout))
  {
  case TYPE_REFUSE:
    show_debug_info(INVALID_BASIC_SETTING_GROUP);
    return FAIL;

  case TYPE_ACK:
    show_debug_info(RECEIVED_EMPTY_BASIC_SETTING_GROUP);
    return FAIL;

  case TYPE_BASIC_SETTING:
  {
    show_debug_info(RECEIVED_BASIC_SETTING);

    // Determine the amount of values received.
    amount_of_values = bytes_received;

    // Determine how many values to copy into the given buffer.
    uint8_t max_values_inreceive_buffer = sizeof(receive_buffer); // how many values would fit in the receive buffer
    uint8_t max_values_in_array = basic_setting_buffer_size;      // how many values would fit in the given array
    uint8_t values_to_copy = min(amount_of_values, min(max_values_inreceive_buffer, max_values_in_array));

    // Copy the values from the RX buffer into the given array.
    memcpy(basic_setting_buffer, receive_buffer, values_to_copy);
    return SUCCESS;
  }
  break;

  default:
    show_debug_info(UNEXPECTED_RESPONSE);
    return ERROR;
  }
}

/**
  Function:
    getBasicSettingValue(uint8_t value_index, uint8_t amount_of_values, uint8_t basic_setting_buffer[], size_t basic_setting_buffer_size)

  Parameters:
    value_index               -> index of the value that needs to be retrieved
    amount_of_values          -> total number of values stored in the array (value passed as reference to basicSetting())
    basic_setting_buffer      -> array in which fault codes have been stored by basicSetting()
    basic_setting_buffer_size -> total size of the given array (provided with the sizeof() operator)

  Returns:
    uint8_t -> value

  Description:
    Provides a measured value from a buffer filled by basicSetting().

  Notes:
    *It is a static function, so it does not require an instance to be used.
*/
uint8_t KLineKWP1281Lib::getBasicSettingValue(uint8_t value_index, uint8_t amount_of_values, uint8_t *basic_setting_buffer, size_t basic_setting_buffer_size)
{
  // If an invalid buffer was provided, return 0.
  if (!basic_setting_buffer || !basic_setting_buffer_size)
  {
    return 0;
  }

  // Check if the array is large enough to contain as many values as "declared" by the value given.
  if (basic_setting_buffer_size < amount_of_values)
  {
    amount_of_values = basic_setting_buffer_size;
  }

  // If the requested index is out of range (or the buffer doesn't contain fault codes), return 0.
  if (value_index >= amount_of_values)
  {
    return 0;
  }

  // Determine the value and return it.
  uint8_t value = basic_setting_buffer[value_index];
  return value;
}

/**
  Function:
    readGroup(uint8_t &amount_of_measurements, uint8_t group, uint8_t measurement_buffer[], size_t measurement_buffer_size)

  Parameters:
    amount_of_measurements  -> will contain the total number of measurements in the group
    group                   -> measurement group to read
    measurement_buffer      -> array into which to store the measurements
    measurement_buffer_size -> total (maximum) length of the given array

  Returns:
    executionStatus -> whether or not the operation executed successfully
       SUCCESS
       FAIL
       ERROR
       GROUP_BASIC_SETTINGS -> buffer contains 10 raw values
       GROUP_HEADER -> buffer contains the header of a header+body response
       GROUP_BODY -> buffer contains the body of a header+body response

  Description:
    Reads a measurement group.

  Notes:
    *When receiving GROUP_HEADER, it should be "set aside", because its data will be needed when receiving GROUP_BODY.
    *Alternatively, always use readGroup() with a large enough buffer, and if you receive GROUP_HEADER, switch to a smaller buffer for receiving GROUP_BODY,
    because that response is generally 4 bytes long, compared to the header which can be even more than 80 bytes long.
    
    *For header+body responses, you need to use the function with suffixes "FromHeader" or "FromHeaderBody".
    *For example, instead of getMeasurementValue(), you will use getMeasurementValueFromHeaderBody().
*/
KLineKWP1281Lib::executionStatus KLineKWP1281Lib::readGroup(uint8_t &amount_of_measurements, uint8_t group, uint8_t *measurement_buffer, size_t measurement_buffer_size)
{
  // If an error occurs, report 0 measurements.
  amount_of_measurements = 0;

  // On non-null groups, request the measurements normally.
  if (group)
  {
    uint8_t parameters[] = {group};
    if (!send_message(KWP_REQUEST_GROUP_READING, parameters, sizeof(parameters)))
    {
      show_debug_info(SEND_ERROR);
      return ERROR;
    }
  }
  // Group 0 is requested separately (and the response is usually of type TYPE_BASIC_SETTING).
  else
  {
    if (!send_message(KWP_REQUEST_GROUP_READING_0))
    {
      show_debug_info(SEND_ERROR);
      return ERROR;
    }
  }

  size_t bytes_received;
  switch (receive_message(&bytes_received, measurement_buffer, measurement_buffer_size, responseTimeout))
  {
  case TYPE_REFUSE:
    show_debug_info(INVALID_MEASUREMENT_GROUP);
    return FAIL;

  case TYPE_ACK:
    show_debug_info(RECEIVED_EMPTY_GROUP);
    return FAIL;
  
  case TYPE_GROUP_HEADER:
  {
    show_debug_info(RECEIVED_GROUP_HEADER);
    
    // If the group header can't fit in the given buffer, it's considered a fatal error.
    if (bytes_received > measurement_buffer_size)
    {
      show_debug_info(ARRAY_NOT_LARGE_ENOUGH);
      return ERROR;
    }
    
    // Go through each measurement.
    size_t buffer_index = 0;
    while (buffer_index < bytes_received)
    {
      // Determine the formula of the current measurement.
      uint8_t formula = measurement_buffer[buffer_index];
      
      // Determine the length of the table attached to the current measurement.
      uint8_t table_length = measurement_buffer[buffer_index + 2];
      
      // For formulas 8B (RPM), 8C (Temperature), 8D (Strings) and 93 (Inclination), the table length should not be 0.
      // For all of them except formula 8D, the table length should be 17.
      if (((formula == 0x8B || formula == 0x8C || formula == 0x93) && table_length != 17) || (formula == 0x8D && table_length == 0))
      {
        show_debug_info(INVALID_GROUP_HEADER_MAP_LENGTH, table_length);
        return ERROR;
      }
      
      // Count the measurement.
      amount_of_measurements++;
      
      // Skip over the bytes FORMULA, NWB, TABLE_LEN, TABLE.
      buffer_index += (3 + table_length);
    }
  }
  return GROUP_HEADER;
  
  case TYPE_BASIC_SETTING: // TYPE_GROUP_BODY, same opcode
  {
    show_debug_info(RECEIVED_GROUP_BODY_OR_BASIC_SETTING);
    
    // If the measurements can't fit in the given buffer, it's considered a fatal error.
    if (bytes_received > measurement_buffer_size)
    {
      show_debug_info(ARRAY_NOT_LARGE_ENOUGH);
      return ERROR;
    }
    
    // I assume the "basic settings" response type is always 10 bytes long.
    if (bytes_received == 10)
    {
      // The `measurement_buffer` contains 10 raw values is its first 10 bytes, do with them as you wish.
      return GROUP_BASIC_SETTINGS;
    }
    // I assume the body of the "header+body" response type is always at most 4 bytes long.
    else if (bytes_received <= 4)
    {
      // Each byte represents the MWb of a measurement.
      amount_of_measurements = bytes_received;
      return GROUP_BODY;
    }
    else
    {
      show_debug_info(INVALID_GROUP_BODY_OR_BASIC_SETTING_LENGTH, bytes_received);
    }
  }
  return ERROR;

  case TYPE_GROUP_READING:
  {
    show_debug_info(RECEIVED_GROUP);

    // If the measurements can't fit in the given buffer, it's considered a fatal error.
    // This is because it might not be possible to count how many measurements were received, if the data is incomplete.
    if (bytes_received > measurement_buffer_size)
    {
      show_debug_info(ARRAY_NOT_LARGE_ENOUGH);
      return ERROR;
    }

    // Count each measurement.
    size_t buffer_index = 0;
    while (buffer_index < bytes_received)
    {
      // Determine the current measurement's formula.
      uint8_t formula = measurement_buffer[buffer_index];
      
      // Determine the current measurement's length.
      uint8_t measurement_length = get_measurement_length(measurement_buffer, bytes_received, buffer_index);
      
      // For formula 3F (ASCII text with unknown length), we need to artificially introduce a length byte, so it looks like formula 5F (ASCII text with known
      // length), to make it possible to process later.
      if (formula == 0x3F)
      {
        // We need one more byte.
        if (bytes_received >= measurement_buffer_size)
        {
          show_debug_info(ARRAY_NOT_LARGE_ENOUGH);
          return ERROR;
        }
        
        // Formula 3F "eats" all bytes after it.
        // So, the function get_measurement_length() returns the amount of bytes from 3F to the end of the message.
        // Since we have an extra byte of space available, we can move everything after 3F by one byte to the right.
        memmove(&measurement_buffer[buffer_index + 2], &measurement_buffer[buffer_index + 1], measurement_length - 1);
        
        // Insert our artificial data length byte after 3F.
        measurement_buffer[buffer_index + 1] = measurement_length - 1;
        
        // "Convert" the formula to 5F.
        measurement_buffer[buffer_index] = 0x5F;
        
        // By definition, formula 3F will be the last in the group, so nothing else needs to be changed.
      }
      
      // Increment the buffer index by the measurement's length.
      buffer_index += measurement_length;
      
      // Count the measurement.
      amount_of_measurements++;
    }
  }
  return SUCCESS;

  default:
    show_debug_info(UNEXPECTED_RESPONSE);
    return ERROR;
  }
}

/**
  Function:
    getFormula(uint8_t measurement_index, uint8_t amount_of_measurements, uint8_t measurement_buffer[], size_t measurement_buffer_size)

  Parameters:
    measurement_index       -> index of the measurement whose formula must be determined (0-4)
    amount_of_measurements  -> total number of measurements stored in the array (value passed as reference to readGroup())
    measurement_buffer      -> array in which measurements have been stored by readGroup()
    measurement_buffer_size -> total size of the given array (provided with the sizeof() operator)

  Returns:
    uint8_t -> formula byte

  Description:
    Retrieves the formula byte for a measurement from a buffer filled by readGroup().

  Notes:
    *Most measurements have 3 bytes. The formula is the first byte.
    *It is a static function, so it does not require an instance to be used.
*/
uint8_t KLineKWP1281Lib::getFormula(uint8_t measurement_index, uint8_t amount_of_measurements, uint8_t *measurement_buffer, size_t measurement_buffer_size)
{
  // If the requested index is out of range (or the buffer doesn't contain measurements), return 0.
  if (measurement_index >= amount_of_measurements)
  {
    return 0;
  }

  // Go through each measurement, returning the formula when on the requested index.
  size_t buffer_index = 0;
  for (uint8_t current_measurement = 0; current_measurement < amount_of_measurements && buffer_index < measurement_buffer_size; current_measurement++)
  {
    // Determine the current measurement's formula.
    uint8_t formula = measurement_buffer[buffer_index];

    // If currently on the requested index, return the formula.
    if (current_measurement == measurement_index)
    {
      return formula;
    }
    
    // Increment the buffer index by the measurement's length.
    buffer_index += get_measurement_length(measurement_buffer, 0, buffer_index);
  }

  return 0;
}

/**
  Function:
    getNWb(uint8_t measurement_index, uint8_t amount_of_measurements, uint8_t measurement_buffer[], size_t measurement_buffer_size)

  Parameters:
    measurement_index       -> index of the measurement whose NWb must be determined (0-4)
    amount_of_measurements  -> total number of measurements stored in the array (value passed as reference to readGroup())
    measurement_buffer      -> array in which measurements have been stored by readGroup()
    measurement_buffer_size -> total size of the given array (provided with the sizeof() operator)

  Returns:
    uint8_t -> NWb (byte A)

  Description:
    Retrieves byte A for a measurement from a buffer filled by readGroup().

  Notes:
    *Most measurements have 3 bytes. NWb is the second byte.
    *It is a static function, so it does not require an instance to be used.
*/
uint8_t KLineKWP1281Lib::getNWb(uint8_t measurement_index, uint8_t amount_of_measurements, uint8_t *measurement_buffer, size_t measurement_buffer_size)
{
  // If the requested index is out of range (or the buffer doesn't contain measurements), return 0.
  if (measurement_index >= amount_of_measurements)
  {
    return 0;
  }

  // Go through each measurement, returning NWb when on the requested index.
  size_t buffer_index = 0;
  for (uint8_t current_measurement = 0; current_measurement < amount_of_measurements && buffer_index < measurement_buffer_size; current_measurement++)
  {
    // Determine the current measurement's formula.
    uint8_t formula = measurement_buffer[buffer_index];

    // If currently on the requested index, return NWb.
    if (current_measurement == measurement_index)
    {
      // For a few formulas, NWb doesn't exist.
      if (formula == 0x5F || formula == 0x76 || formula == 0xA0)
      {
        return 0;
      }
      // Otherwise, return the byte after the formula.
      else
      {
        return measurement_buffer[buffer_index + 1];
      }
    }
    
    // Increment the buffer index by the measurement's length.
    buffer_index += get_measurement_length(measurement_buffer, 0, buffer_index);
  }

  return 0;
}

/**
  Function:
    getMWb(uint8_t measurement_index, uint8_t amount_of_measurements, uint8_t measurement_buffer[], size_t measurement_buffer_size)

  Parameters:
    measurement_index       -> index of the measurement whose MWb must be determined (0-4)
    amount_of_measurements  -> total number of measurements stored in the array (value passed as reference to readGroup())
    measurement_buffer      -> array in which measurements have been stored by readGroup()
    measurement_buffer_size -> total size of the given array (provided with the sizeof() operator)

  Returns:
    uint8_t -> MWb (byte B)

  Description:
    Retrieves byte B for a measurement from a buffer filled by readGroup().

  Notes:
    *Most measurements have 3 bytes. NWb is the third byte.
    *It is a static function, so it does not require an instance to be used.
*/
uint8_t KLineKWP1281Lib::getMWb(uint8_t measurement_index, uint8_t amount_of_measurements, uint8_t *measurement_buffer, size_t measurement_buffer_size)
{
  // If the requested index is out of range (or the buffer doesn't contain measurements), return 0.
  if (measurement_index >= amount_of_measurements)
  {
    return 0;
  }

  // Go through each measurement, returning MWb when on the requested index.
  size_t buffer_index = 0;
  for (uint8_t current_measurement = 0; current_measurement < amount_of_measurements && buffer_index < measurement_buffer_size; current_measurement++)
  {
    // Determine the current measurement's formula.
    uint8_t formula = measurement_buffer[buffer_index];

    // If currently on the requested index, return MWb.
    if (current_measurement == measurement_index)
    {
      // For a few formulas, MWb doesn't exist.
      if (formula == 0x5F || formula == 0x76 || formula == 0xA0)
      {
        return 0;
      }
      // Otherwise, return the byte after NWb.
      else
      {
        return measurement_buffer[buffer_index + 2];
      }
    }
    
    // Increment the buffer index by the measurement's length.
    buffer_index += get_measurement_length(measurement_buffer, 0, buffer_index);
  }

  return 0;
}

/**
  Function:
    getMeasurementData(uint8_t measurement_index, uint8_t amount_of_measurements, uint8_t measurement_buffer[], size_t measurement_buffer_size)

  Parameters:
    measurement_index       -> index of the measurement whose data must be determined (0-4)
    amount_of_measurements  -> total number of measurements stored in the array (value passed as reference to readGroup())
    measurement_buffer      -> array in which measurements have been stored by readGroup()
    measurement_buffer_size -> total size of the given array (provided with the sizeof() operator)

  Returns:
    uint8_t* -> measurement data buffer

  Description:
    Retrieves the data for a measurement from a buffer filled by readGroup().

  Notes:
    *Usually, measurements contain 2 data bytes, but, in some special cases, they may contain more.
    *Find out how many bytes the measurement takes with the getMeasurementDataLength() function.
    *It is a static function, so it does not require an instance to be used.
*/
uint8_t *KLineKWP1281Lib::getMeasurementData(uint8_t measurement_index, uint8_t amount_of_measurements, uint8_t *measurement_buffer, size_t measurement_buffer_size)
{
  // If the requested index is out of range (or the buffer doesn't contain measurements), return an invalid buffer.
  if (measurement_index >= amount_of_measurements)
  {
    return nullptr;
  }

  // Go through each measurement, returning the measurement data when on the requested index.
  size_t buffer_index = 0;
  for (uint8_t current_measurement = 0; current_measurement < amount_of_measurements && buffer_index < measurement_buffer_size; current_measurement++)
  {
    // Determine the current measurement's formula.
    uint8_t formula = measurement_buffer[buffer_index];

    // If currently on the requested index, return the measurement data.
    if (current_measurement == measurement_index)
    {
      // For a few formulas, the data starts after the length byte.
      if (formula == 0x5F || formula == 0x76)
      {
        return &measurement_buffer[buffer_index + 2];
      }
      // Otherwise, for most measurements, the data starts after the formula.
      else
      {
        return &measurement_buffer[buffer_index + 1];
      }
    }
    
    // Increment the buffer index by the measurement's length.
    buffer_index += get_measurement_length(measurement_buffer, 0, buffer_index);
  }

  return nullptr;
}

/**
  Function:
    getMeasurementDataLength(uint8_t measurement_index, uint8_t amount_of_measurements, uint8_t measurement_buffer[], size_t measurement_buffer_size)

  Parameters:
    measurement_index       -> index of the measurement whose data length must be determined (0-4)
    amount_of_measurements  -> total number of measurements stored in the array (value passed as reference to readGroup())
    measurement_buffer      -> array in which measurements have been stored by readGroup()
    measurement_buffer_size -> total size of the given array (provided with the sizeof() operator)

  Returns:
    uint8_t -> measurement data length

  Description:
    Retrieves the data length for a measurement from a buffer filled by readGroup().

  Notes:
    *Usually, measurements contain 2 data bytes, but, in some special cases, they may contain more.
    *It is a static function, so it does not require an instance to be used.
*/
uint8_t KLineKWP1281Lib::getMeasurementDataLength(uint8_t measurement_index, uint8_t amount_of_measurements, uint8_t *measurement_buffer, size_t measurement_buffer_size)
{
  // If the requested index is out of range (or the buffer doesn't contain measurements), return 0.
  if (measurement_index >= amount_of_measurements)
  {
    return 0;
  }

  // Go through each measurement, returning the measurement data when on the requested index.
  size_t buffer_index = 0;
  for (uint8_t current_measurement = 0; current_measurement < amount_of_measurements && buffer_index < measurement_buffer_size; current_measurement++)
  {
    // Determine the current measurement's formula.
    uint8_t formula = measurement_buffer[buffer_index];
    
    // Determine the current measurement's length.
    uint8_t measurement_length = get_measurement_length(measurement_buffer, 0, buffer_index);

    // If currently on the requested index, return the measurement data.
    if (current_measurement == measurement_index)
    {
      // For formula 5F (ASCII text with known length) and 76 (HEX bytes with known length), the length is specified in the 2nd byte.
      if (formula == 0x5F || formula == 0x76)
      {
        return measurement_buffer[buffer_index + 1];
      }
      // Otherwise, for most measurements, the data length is the response length minus the formula byte.
      else
      {
        return measurement_length - 1;
      }
    }
    
    // Increment the buffer index by the measurement's length.
    buffer_index += measurement_length;
  }

  return 0;
}

/**
  Function:
    getMeasurementType(uint8_t measurement_index, uint8_t amount_of_measurements, uint8_t measurement_buffer[], size_t measurement_buffer_size)
    getMeasurementType(uint8_t formula)

  Parameters (1):
    measurement_index       -> index of the measurement whose type must be determined (0-4)
    amount_of_measurements  -> total number of measurements stored in the array (value passed as reference to readGroup())
    measurement_buffer      -> array in which measurements have been stored by readGroup()
    measurement_buffer_size -> total size of the given array (provided with the sizeof() operator)

  Parameters (2):
    formula -> byte returned by getFormula()

  Returns:
    measurementType ->
      UNKNOWN - requested measurement outside range
      VALUE   - get value with getMeasurementValue() and units with getMeasurementUnits()
      TEXT    - get text with getMeasurementText() and, sometimes, original value with getMeasurementValue()

  Description:
    Determines the type of a measurement from a buffer filled by readGroup().

  Notes:
    *If an invalid measurement_index is specified, the returned type is UNKNOWN.
    *Even if the measurement is of type TEXT, its value might contain the "origin" of the string, like a code or 16-bit value so it can be used without
    necessarily checking the text itself.
    *For example, for a clock measurement, the text string will contain "hh:mm", but the value will also be hh.mm (hours before decimal, minutes after).
    *It is a static function, so it does not require an instance to be used.
*/
KLineKWP1281Lib::measurementType KLineKWP1281Lib::getMeasurementType(uint8_t measurement_index, uint8_t amount_of_measurements, uint8_t *measurement_buffer, size_t measurement_buffer_size)
{
  // Determine the formula.
  uint8_t formula = getFormula(measurement_index, amount_of_measurements, measurement_buffer, measurement_buffer_size);

  // Use the other function.
  return getMeasurementType(formula);
}

KLineKWP1281Lib::measurementType KLineKWP1281Lib::getMeasurementType(uint8_t formula)
{
  // There are no valid formulas after B5.
  if (formula > 0xB5)
  {
    return UNKNOWN;
  }
  
  // Only a few measurement formulas are of the "TEXT" type.
  switch (formula)
  {
  case 0x00: // N/A
  case 0x3F: // ASCII long text with unknown length (was converted to 5F by readGroup())
  case 0x6C: // Environment
  case 0x6D: // N/A
  case 0x6E: // Workshop
    return UNKNOWN;

  // Long
  case 0x5F: // ASCII long text with known length
  case 0x76: // Hex bytes with known length
  // Regular
  case 0x0A: // Warm/Cold
  case 0x10: // Switch positions
  case 0x88: // Switch positions
  case 0x11: // 2 ASCII letters
  case 0x8E: // 2 ASCII letters
  case 0x1D: // Map1/Map2
  case 0x25: // Text from table
  case 0x7B: // Text from table
  case 0x2C: // Time
  case 0x6B: // Hex bytes
  case 0x7F: // Date
  case 0xA1: // Binary bytes
    return TEXT;

  default:
    return VALUE;
  }
}

/**
  Function:
    getMeasurementValue(uint8_t measurement_index, uint8_t amount_of_measurements, uint8_t measurement_buffer[], size_t measurement_buffer_size)
    getMeasurementValue(uint8_t formula, uint8_t measurement_data[], uint8_t measurement_data_length)
    getMeasurementValue(uint8_t formula, uint8_t NWb, uint8_t MWb)

  Parameters (1):
    measurement_index       -> index of the measurement that needs to be calculated (0-3)
    amount_of_measurements  -> total number of measurements stored in the array (value passed as reference to readGroup())
    measurement_buffer      -> array in which measurements have been stored by readGroup()
    measurement_buffer_size -> total size of the given array (provided with the sizeof() operator)

  Parameters (2):
    formula                 -> byte returned by getFormula()
    measurement_data        -> buffer returned by getMeasurementData()
    measurement_data_length -> byte returned by getMeasurementDataLength()

  Parameters (3):
    formula -> byte returned by getFormula()
    NWb     -> byte returned by getNWb()
    MWb     -> byte returned by getMWb()

  Returns:
    double -> calculated value

  Description:
    Calculates the actual value of a measurement from a buffer filled by readGroup().

  Notes:
    *If an invalid measurement_index is specified, the returned value is nan.
    *It is a static function, so it does not require an instance to be used.
*/
double KLineKWP1281Lib::getMeasurementValue(uint8_t measurement_index, uint8_t amount_of_measurements, uint8_t *measurement_buffer, size_t measurement_buffer_size)
{
  // Determine the formula.
  uint8_t formula = getFormula(measurement_index, amount_of_measurements, measurement_buffer, measurement_buffer_size);

  // Retrieve the measurement data and its length.
  uint8_t *measurement_data = getMeasurementData(measurement_index, amount_of_measurements, measurement_buffer, measurement_buffer_size);
  uint8_t measurement_data_length = getMeasurementDataLength(measurement_index, amount_of_measurements, measurement_buffer, measurement_buffer_size);

  // Use the other function.
  return getMeasurementValue(formula, measurement_data, measurement_data_length);
}

double KLineKWP1281Lib::getMeasurementValue(uint8_t formula, uint8_t *measurement_data, uint8_t measurement_data_length)
{
  // If an invalid buffer was provided, return nan.
  if (!measurement_data || !measurement_data_length)
  {
    return 0.0 / 0.0;
  }
  
  // Handle formula A0 (measurement with variable units).
  if (formula == 0xA0)
  {
    // The data length must be 5.
    if (measurement_data_length != 5)
    {
      return 0.0 / 0.0;
    }
    
    // Combine BYTE_B and BYTE_C.
    int32_t WORD_BC = (measurement_data[1] << 8) | measurement_data[2];
    
    // Negate the WORD if the highest bit of BYTE_D is set.
    // Don't negate it if units call for degrees ATDC/BTDC, the value should always be positive.
    if ((measurement_data[3] & 0x80) && ((measurement_data[4] & 0x3F) != 0x23))
    {
      WORD_BC = -WORD_BC;
    }
    
    // Separate BYTE_D into halves.
    // The upper half should not contain the highest bit of BYTE_D, that was used for the value's sign.
    uint8_t BYTE_DL = measurement_data[3] & 0xF;
    uint8_t BYTE_DH = (measurement_data[3] >> 4) & 7;
    
    // Calculate the value.
    // BYTE_DH refers to a negative power of 10, so it will be used as an index into that array.
    // Having been AND-ed with 7, the maximum value of BYTE_DH is 7; the array has 8 values, so this is safe.
    return measurement_data[0] * WORD_BC * BYTE_DL * negative_pow_10[BYTE_DH];
  }
  // If it's any other formula, the data length must be 2.
  else if (measurement_data_length != 2)
  {
    return 0.0 / 0.0;
  }

  // Determine NWb and MWb.
  uint8_t NWb = measurement_data[0];
  uint8_t MWb = measurement_data[1];

  // Use the other function.
  return getMeasurementValue(formula, NWb, MWb);
}

double KLineKWP1281Lib::getMeasurementValue(uint8_t formula, uint8_t NWb, uint8_t MWb)
{
  double NW = NWb, MW = MWb;
  switch (formula)
  {
    //0x00 - UNKNOWN                                                                    //N/A                    [N/A]
    case 0x01: return MW * NW * 0.2;                                                    //RPM                    [/min]
    case 0x02: return MW * NW * 0.002;                                                  //Load                   [%]
    case 0x03: return MW * NW * 0.002;                                                  //Throttle angle         [deg]
    case 0x04: return (MW - 127) * NW * -0.01;                                          //Ignition angle         [deg]
    case 0x05: return (MW - 100) * NW * 0.1;                                            //Temperature            [degC]
    case 0x06: return MW * NW * 0.001;                                                  //Voltage                [V]
    case 0x07: return MW * NW * 0.01;                                                   //Speed                  [km/h]
    case 0x08: return MW * NW * 0.1;                                                    //N/A                    [N/A]
    case 0x09: return (MW - 127) * NW * 0.02;                                           //Steering angle         [deg]
    case 0x0A: return (MW ? 1 : 0);                                                     //Text                   [Warm/Cold]
    case 0x0B: return 1 + ((MW - 128) * NW * 0.0001);                                   //Lambda factor          [N/A]
    case 0x0C: return MW * NW * 0.001;                                                  //Resistance             [Ohm]
    case 0x0D: return (MW - 127) * NW * 0.001;                                          //Distance               [mm]
    case 0x0E: return MW * NW * 0.005;                                                  //Pressure               [bar]
    case 0x0F: return MW * NW * 0.01;                                                   //Injection time         [ms]
    case 0x10: return MWb & NWb;                                                        //Switch positions       [bin(MWb&NWb)]
    case 0x11: return To16Bit(NW, MW);                                                  //2 ASCII letters        [char(NWb)char(MWb)]
    case 0x12: return MW * NW * 0.04;                                                   //Absolute pressure      [mbar]
    case 0x13: return MW * NW * 0.01;                                                   //Fuel level             [l]
    case 0x14: return (MW - 128) * NW * 1 / 128;                                        //Lambda integrator      [%]
    case 0x15: return MW * NW * 0.001;                                                  //Voltage                [V]
    case 0x16: return MW * NW * 0.001;                                                  //Injection time         [ms]
    case 0x17: return MW * NW * 1 / 256;                                                //Duty cycle             [%]
    case 0x18: return MW * NW * 0.001;                                                  //Current                [A]
    case 0x19: return (256 * MW + NW) * 1 / 180;                                        //Mass flow              [g/s]
    case 0x1A: return MW - NW;                                                          //Temperature            [degC]
    case 0x1B: return (MW - 128) * NW * 0.01;                                           //Ignition angle         [deg]
    case 0x1C: return MW - NW;                                                          //N/A                    [N/A]
    case 0x1D: return ((MW < NW) ? 1 : 2);                                              //Map number             [Map1/Map2]
    case 0x1E: return MW * NW * 1 / 12;                                                 //Knock correction depth [deg]
    case 0x1F: return MW * NW * 1 / 2560;                                               //N/A                    [N/A]
    case 0x20: return ToSigned(MW);                                                     //N/A                    [N/A]
    case 0x21: return (MW / NW) * 100;                                                  //Load                   [%]
    case 0x22: return (MW - 128) * NW * 0.01;                                           //Idle correction        [KW]
    case 0x23: return MW * NW * 0.01;                                                   //Fuel rate              [l/h]
    case 0x24: return To16Bit(NW, MW) * 10;                                             //Mileage                [km]
    case 0x25: return To16Bit(NW, MW);                                                  //Text from table        [text]
    case 0x26: return (MW - 128) * NW * 0.001;                                          //Segment correction     [KW]
    case 0x27: return NW * MW * 1 / 255;                                                //Injection mass         [mg/h]
    case 0x28: return ((NW * 255 + MW) - 4000) * 0.1;                                   //Current                [A]
    case 0x29: return NW * 255 + MW;                                                    //Charge                 [N/A]
    case 0x2A: return ((NW * 255 + MW) - 4000) * 0.1;                                   //Power                  [kW]
    case 0x2B: return (NW * 255 + MW) * 0.1;                                            //Battery voltage        [V]
    case 0x2C: return MW / 100.0 + NW;                                                  //Time                   [hh:mm]
    case 0x2D: return MW * NW * 0.1;                                                    //Fuel rate              [l/km]
    case 0x2E: return ((NW * 256) + MW - 32768) * 0.00275;                              //Segment correction     [KW]
    case 0x2F: return (MW - 128) * NW;                                                  //Time correction        [ms]
    case 0x30: return NW * 255 + MW;                                                    //N/A                    [N/A]
    case 0x31: return NW * MW * 0.025;                                                  //Air mass per stroke    [mg/h]
    case 0x32: return (MW - 128) * 100 / NW;                                            //Pressure               [mbar]
    case 0x33: return (MW - 128) * NW / 255;                                            //Injection mass         [mg/h]
    case 0x34: return (MW - 50) * NW * 0.02;                                            //Torque                 [Nm]
    case 0x35: return ((MW - 128) * 256 + NW) / 180;                                    //Mass flow              [g/s]
    case 0x36: return NW * 256 + MW;                                                    //Count                  [N/A]
    case 0x37: return MW * NW * 0.005;                                                  //Time                   [s]
    case 0x38: return (NWb << 8) & MWb;                                                 //Code for WSC16=0       [N/A]
    case 0x39: return ((NWb << 8) & MWb) + 65536;                                       //Code for WSC16=1       [N/A]
    case 0x3A: return ToSigned(MW) * 1.023;                                             //Misfires               [/s]
    case 0x3B: return To16Bit(NW, MW) * 1 / 32768;                                      //Segment correction     [N/A]
    case 0x3C: return To16Bit(NW, MW) * 1 / 100;                                        //Time resolution        [s]
    case 0x3D: return (MW - 128) / NW;                                                  //Difference             [N/A]
    case 0x3E: return MW * NW * 256 / 1000;                                             //Time                   [s]
    //0x3F - TEXT (converted to 5F by readGroup())                                      //ASCII long text        [text]
    case 0x40: return MW + NW;                                                          //Resistance             [Ohm]
    case 0x41: return (MW - 127) * NW * 0.01;                                           //Distance               [mm]
    case 0x42: return MW * NW / 512;                                                    //Voltage                [V]
    case 0x43: return ToSigned(NW, MW) * 2.5;                                           //Steering angle         [deg]
    case 0x44: return ToSigned(NW, MW) * 0.1358;                                        //Turn rate              [deg/s]
    case 0x45: return ToSigned(NW, MW) * 0.3255;                                        //Pressure               [bar]
    case 0x46: return ToSigned(NW, MW) * 0.192;                                         //Lat. acceleration      [m/s^2]
    case 0x47: return MW * NW;                                                          //Distance               [cm]
    case 0x48: return (NW * 255 + MW * (211 - NW)) / 4080;                              //Voltage                [V]
    case 0x49: return NW * MW * 0.01;                                                   //Resistance             [Ohm]
    case 0x4A: return 0.1 * NW * MW;                                                    //Time                   [months]
    case 0x4B: return NW * 256 + MW;                                                    //Fault code             [N/A]
    case 0x4C: return NW * 255 + MW;                                                    //Resistance             [kOhm]
    case 0x4D: return (255 * NW + MW * 60) / 4080;                                      //Voltage                [V]
    case 0x4E: return ToSigned(MW) * 1.819;                                             //Misfires               [/s]
    case 0x4F: return MW;                                                               //Channel number         [N/A]
    case 0x50: return To16Bit(NW, MW) / 100;                                            //Resistance             [kOhm]
    case 0x51: return ToSigned(NW, MW) * 0.04375;                                       //Steering angle         [deg]
    case 0x52: return ToSigned(NW, MW) * 0.00981;                                       //Lat. acceleration      [m/s^2]
    case 0x53: return ToSigned(NW, MW) * 0.01;                                          //Pressure               [bar]
    case 0x54: return ToSigned(NW, MW) * 0.0973;                                        //Long. acceleration     [m/s^2]
    case 0x55: return ToSigned(NW, MW) * 0.002865;                                      //Turn rate              [deg/s]
    case 0x56: return MW * NW * 0.1;                                                    //Current                [A]
    case 0x57: return NW * (MW - 128) * 0.1;                                            //Turn rate              [deg/s]
    case 0x58: return MW * NW * 0.01;                                                   //Resistance             [kOhm]
    case 0x59: return NW * 256 + MW;                                                    //On-time                [h]
    case 0x5A: return NW * MW * 0.1;                                                    //Weight                 [kg]
    case 0x5B: return NW * (MW - 128) * 0.1;                                            //Steering angle         [deg]
    case 0x5C: return NW * MW;                                                          //Mileage                [km]
    case 0x5D: return (MW - 128) * NW * 0.001;                                          //Torque                 [Nm]
    case 0x5E: return NW * (MW - 128) * 0.1;                                            //Torque                 [Nm]
    //0x5F - TEXT                                                                       //ASCII long text        [text]
    case 0x60: return NW * MW * 0.1;                                                    //Pressure               [mbar]
    case 0x61: return (MW - NW) * 5;                                                    //Cat-temperature        [degC]
    case 0x62: return NW * MW * 0.1;                                                    //Impulses               [/km]
    case 0x63: return ToSigned(NW, MW);                                                 //N/A                    [N/A]
    case 0x64: return NW * MW * 0.1;                                                    //Pressure               [bar]
    case 0x65: return MW * NW * 0.001;                                                  //Fuel level factor      [l/mm]
    case 0x66: return MW * NW * 0.1;                                                    //Fuel level             [mm]
    case 0x67: return NW + MW * 0.05;                                                   //Voltage                [V]
    case 0x68: return (MW - 128) * NW * 0.2;                                            //Volume                 [ml]
    case 0x69: return (MW - 128) * NW * 0.01;                                           //Distance               [m]
    case 0x6A: return (MW - 128) * NW * 0.1;                                            //Speed                  [km/h]
    case 0x6B: return ToSigned(NW, MW);                                                 //Hex bytes              [hex(NWb)hex(MWb)]
    //0x6C - UNKNOWN                                                                    //Environment            [N/A]
    //0x6D - UNKNOWN                                                                    //N/A                    [N/A]
    //0x6E - UNKNOWN                                                                    //Workshop               [N/A]
    case 0x6F: return 0x6F0000 | (NWb << 8) | MWb;                                      //Mileage                [km]
    case 0x70: return (MW - 128) * NW * 0.001;                                          //Angle                  [deg]
    case 0x71: return (MW - 128) * NW * 0.01;                                           //N/A                    [N/A]
    case 0x72: return (MW - 128) * NW;                                                  //Altitude               [m]
    case 0x73: return ToSigned(NW, MW);                                                 //Power                  [W]
    case 0x74: return ToSigned(NW, MW);                                                 //RPM                    [/min]
    case 0x75: return (MW - 64) * NW * 0.01;                                            //Temperature            [degC]
    //0x76 - TEXT                                                                       //Hex bytes              [hex]
    case 0x77: return MW * NW * 0.01;                                                   //Percentage             [%]
    case 0x78: return MW * NW * 1.41;                                                   //Angle                  [deg]
    case 0x79: return ((MW * 256) + NW) * 0.5;                                          //N/A                    [N/A]
    case 0x7A: return ((MW * 256) + NW - 32768) * 0.01;                                 //N/A                    [N/A]
    case 0x7B: return To16Bit(NW, MW);                                                  //Text from table        [text]
    case 0x7C: return MW * NW * 0.1;                                                    //Current                [mA]
    case 0x7D: return NW - MW;                                                          //Attenuation            [dB]
    case 0x7E: return NW * MW * 0.1;                                                    //N/A                    [N/A]
    case 0x7F: return                                                                   //Date                   [yyyy:mm:dd]
        200000 + (MWb & 0x7F) * 100
        + (((NWb & 0x07) << 1) | ((MWb & 0x80) >> 7))
        + ((NWb & 0xF8) >> 3) * 0.01;
    case 0x80: return MW * NW;                                                          //RPM                    [/min]
    case 0x81: return MW * NW * 1 / 256;                                                //Load                   [%]
    case 0x82: return MW * NW * 1 / 2560;                                               //Current                [A]
    case 0x83: return (MW * NW * 0.5) - 30;                                             //Ignition angle         [deg]
    case 0x84: return MW * NW * 0.5;                                                    //Throttle angle         [deg]
    case 0x85: return MW * NW * 1 / 256;                                                //Voltage                [V]
    case 0x86: return MW * NW;                                                          //Speed                  [km/h]
    case 0x87: return MW * NW;                                                          //N/A                    [N/A]
    case 0x88: return MWb & NWb;                                                        //Switch positions       [bin(MWb&nWb)]
    case 0x89: return MW * NW * 0.01;                                                   //Injection time         [ms]
    case 0x8A: return MW * NW * 0.001;                                                  //Knock sensor           [V]
    //0x8B - value mapped through table provided by module                              //RPM                    [/min]
    //0x8C - value mapped through table provided by module                              //Temperature            [degC]
    //0x8D - ASCII string from list provided by module                                  //Text from table        [text]
    case 0x8E: return To16Bit(NW, MW);                                                  //2 ASCII letters        [char(NWb)char(MWb)]
    case 0x8F: return (MW - 128) * NW * 0.01;                                           //Ignition angle         [deg]
    case 0x90: return MW * NW * 0.01;                                                   //Consumption            [l/h]
    case 0x91: return MW * NW * 0.01;                                                   //Consumption            [N/A]
    case 0x92: return 1 + ((MW - 128) * NW * 0.0001);                                   //Lambda factor          [N/A]
    //0x93 - value mapped through table provided by module                              //Inclination            [%]
    case 0x94: return (MW - 128) * NW * 0.25;                                           //Slip RPM               [N/A]
    case 0x95: return (MW - 100) * NW * 0.1;                                            //Temperature            [degC]
    case 0x96: return (256 * MW + NW) * 1 / 180;                                        //Mass flow              [g/s]
    case 0x97: return (MW - 128) * NW * 0.01;                                           //Idle correction        [KW]
    case 0x98: return NW * MW * 0.025;                                                  //Air mass per stroke    [mg/h]
    case 0x99: return (MW - 128) * NW / 255;                                            //Injection quantity     [mg/h]
    case 0x9A: return MW * NW;                                                          //Angle                  [deg]
    case 0x9B: return (NW * MW * 0.01) - 90;                                            //Angle                  [deg]
    case 0x9C: return To16Bit(NW, MW);                                                  //Distance               [cm]
    case 0x9D: return ToSigned(NW, MW);                                                 //Distance               [cm]
    case 0x9E: return To16Bit(NW, MW) * 0.01;                                           //Speed                  [km/h]
    case 0x9F: return ((MW - 127) * 256 + NW) * 0.1;                                    //Temperature            [degC]
    //0xA0 - variable units                                                             //N/A                    [variable]
    case 0xA1: return To16Bit(NW, MW);                                                  //Binary bytes           [bin(NWb)bin(MWb)]
    case 0xA2: return NW * MW * 0.448;                                                  //Angle                  [deg]
    case 0xA3: return MW / 100.0 + NW;                                                  //Time                   [hh:mm]
    case 0xA4: return ((MW <= 100) ? MW : ((MW > 100 && MW <= 200) ? (MW - 100) : MW)); //Percentage             [%]
    case 0xA5: return ((MW * 256) + NW - 32768);                                        //Current                [mA]
    case 0xA6: return ToSigned(NW, MW) * 2.5;                                           //Turn rate              [deg/s]
    case 0xA7: return MW * NW * 0.001;                                                  //Resistance             [mOhm]
    case 0xA8: return (256 * NW + MW) * 0.01;                                           //Percentage             [%]
    case 0xA9: return MW * NW + 200;                                                    //Nernst voltage         [mV]
    case 0xAA: return MW * NW * 1 / 2560;                                               //Ammonia                [g]
    case 0xAB: return MW * NW * 1 / 2560;                                               //Nitrogen oxide         [g]
    case 0xAC: return MW * NW;                                                          //Nitrogen oxide         [mg/km]
    case 0xAD: return MW * NW * 0.1;                                                    //NOx mass flow          [mg/s]
    case 0xAE: return MW * NW * 0.01;                                                   //Average DEF            [km]
    case 0xAF: return MW * NW * 0.005;                                                  //DEF pressure           [bar]
    case 0xB0: return MW * NW * 0.1;                                                    //NOx concentration      [ppm]
    case 0xB1: return ((NW - 128) * 256 + MW);                                          //Torque                 [Nm]
    case 0xB2: return ((NW - 128) * 256 + MW) * ((double)6 / 51);                       //Turn rate              [deg/s]
    case 0xB3: return (NW * 256 + MW) * 10;                                             //Quantity               [N/A]
    case 0xB4: return MW * NW * 1 / 256;                                                //Mass flow              [kg/h]
    case 0xB5: return MW * NW * 0.001;                                                  //Mass flow              [mg/s]
  }

  return 0.0 / 0.0;
}

/**
  Function:
    getMeasurementUnits(uint8_t measurement_index, uint8_t amount_of_measurements, uint8_t measurement_buffer[], size_t measurement_buffer_size, char str[], size_t string_size)
    getMeasurementUnits(uint8_t formula, uint8_t measurement_data[], uint8_t measurement_data_length, char str[], size_t string_size)
    getMeasurementUnits(uint8_t formula, uint8_t NWb, uint8_t MWb, char str[], size_t string_size)

  Parameters (1):
    measurement_index       -> index of the measurement whose units must be determined (0-4)
    amount_of_measurements  -> total number of measurements stored in the array (value passed as reference to readGroup())
    measurement_buffer      -> array in which measurements have been stored by readGroup()
    measurement_buffer_size -> total size of the given array (provided with the sizeof() operator)
    str                     -> string (character array) into which to copy the units
    string_size             -> total size of the given array (provided with the sizeof() operator)

  Parameters (2):
    formula                 -> byte returned by getFormula()
    measurement_data        -> buffer returned by getMeasurementData()
    measurement_data_length -> byte returned by getMeasurementDataLength()
    str                     -> string (character array) into which to copy the units
    string_size             -> total size of the given array (provided with the sizeof() operator)

  Parameters (3):
    formula     -> byte returned by getFormula()
    NWb         -> byte returned by getNWb()
    MWb         -> byte returned by getMWb()
    str         -> string (character array) into which to copy the units
    string_size -> total size of the given array (provided with the sizeof() operator)

  Returns:
    char* -> the same character array provided (str)

  Description:
    Provides a string containing the proper units for a measurement of type VALUE.

  Notes:
    *It is a static function, so it does not require an instance to be used.
*/
char *KLineKWP1281Lib::getMeasurementUnits(uint8_t measurement_index, uint8_t amount_of_measurements, uint8_t *measurement_buffer, size_t measurement_buffer_size, char *str, size_t string_size)
{
  // Determine the formula.
  uint8_t formula = getFormula(measurement_index, amount_of_measurements, measurement_buffer, measurement_buffer_size);

  // Retrieve the measurement data and its length.
  uint8_t *measurement_data = getMeasurementData(measurement_index, amount_of_measurements, measurement_buffer, measurement_buffer_size);
  uint8_t measurement_data_length = getMeasurementDataLength(measurement_index, amount_of_measurements, measurement_buffer, measurement_buffer_size);

  // Use the other function.
  return getMeasurementUnits(formula, measurement_data, measurement_data_length, str, string_size);
}

char *KLineKWP1281Lib::getMeasurementUnits(uint8_t formula, uint8_t *measurement_data, uint8_t measurement_data_length, char *str, size_t string_size)
{
  // If an invalid buffer was provided, return an invalid string.
  if (!measurement_data || !measurement_data_length)
  {
    return nullptr;
  }
  
  // Handle formula A0 (measurement with variable units).
  if (formula == 0xA0)
  {
    // The data length must be 5.
    if (measurement_data_length != 5)
    {
      return nullptr;
    }
    
    // Determine the units' prefix.
    bool has_units_prefix = true;
    switch (measurement_data[4] >> 6)
    {
      case 0: has_units_prefix = false; break;
      case 1: str[0] = 'k'; break;
      case 2: str[0] = 'm'; break;
      case 3: str[0] = 'u'; break;
    }
    
    // This will point to one of the strings stored in from "units.h", which will be copied into the given string.
    const char *unit_pointer = nullptr;
    switch (measurement_data[4] & 0x3F)
    {
    case 0x01: unit_pointer = KWP_units_Voltage; break;
    case 0x02: unit_pointer = KWP_units_Vss; break;
    case 0x03: unit_pointer = KWP_units_Current; break;
    case 0x04: unit_pointer = KWP_units_Capacity; break;
    case 0x05: unit_pointer = KWP_units_Resistance; break;
    case 0x06: unit_pointer = KWP_units_Power; break;
    case 0x07: unit_pointer = KWP_units_Wm2; break;
    case 0x08: unit_pointer = KWP_units_Wcm2; break;
    case 0x09: unit_pointer = KWP_units_Wh; break;
    case 0x0A: unit_pointer = KWP_units_Ws; break;
    case 0x0B: unit_pointer = KWP_units_Distance; break;
    case 0x0C: unit_pointer = KWP_units_ms; break;
    case 0x0D: unit_pointer = KWP_units_Acceleration; break;
    case 0x0E: unit_pointer = KWP_units_Distance_c; break;
    case 0x0F: unit_pointer = KWP_units_Speed; break;
    case 0x10: unit_pointer = KWP_units_Volume; break;
    case 0x11: unit_pointer = KWP_units_Consumption_100km; break;
    case 0x12: unit_pointer = KWP_units_Fuel_Level_Factor; break;
    case 0x13: unit_pointer = KWP_units_Consumption_h; break;
    case 0x14: unit_pointer = KWP_units_lkm; break;
    case 0x15: unit_pointer = KWP_units_Time; break;
    case 0x16: unit_pointer = KWP_units_Time_h; break;
    case 0x17: unit_pointer = KWP_units_Time_mo; break;
    case 0x18: unit_pointer = KWP_units_Mass; break;
    case 0x19: unit_pointer = KWP_units_Mass_Flow; break;
    case 0x1A: unit_pointer = KWP_units_Mass_Per_Stroke_m; break;
    case 0x1B: unit_pointer = KWP_units_Torque; break;
    case 0x1C: unit_pointer = KWP_units_N; break;
    case 0x1D: unit_pointer = KWP_units_Pressure; break;
    case 0x1E: unit_pointer = KWP_units_Angle; break;
    case 0x1F: unit_pointer = KWP_units_angdeg; break;
    case 0x20: unit_pointer = KWP_units_Temperature; break;
    case 0x21: unit_pointer = KWP_units_degF; break;
    case 0x22: unit_pointer = KWP_units_Turn_Rate; break;
    case 0x23: break;
    case 0x24: unit_pointer = KWP_units_RPM; break;
    case 0x25: unit_pointer = KWP_units_Percentage; break;
    case 0x26: unit_pointer = KWP_units_Correction; break;
    case 0x27: unit_pointer = KWP_units_Correction; break;
    case 0x28: unit_pointer = KWP_units_Misfires; break;
    case 0x29: unit_pointer = KWP_units_Imp; break;
    case 0x2A: unit_pointer = KWP_units_Attenuation; break;
    default:
      str[0] = '\0';
      return str;
    }
    
    // Handle units degATDC/BTDC, which depend on the sign of the received value.
    if ((measurement_data[4] & 0x3F) == 0x23)
    {
      if (measurement_data[3] & 0x80) // negative
      {
        unit_pointer = KWP_units_Ignition_BTDC;
      }
      else // positive
      {
        unit_pointer = KWP_units_Ignition_ATDC;
      }
    }
    
    strncpy_P(str + has_units_prefix, unit_pointer, string_size - has_units_prefix);
    str[string_size - 1] = '\0';
    return str;
  }
  // If it's any other formula, the data length must be 2.
  else if (measurement_data_length != 2)
  {
    return nullptr;
  }

  // Determine NWb and MWb.
  uint8_t NWb = measurement_data[0];
  uint8_t MWb = measurement_data[1];

  // Use the other function.
  return getMeasurementUnits(formula, NWb, MWb, str, string_size);
}

char *KLineKWP1281Lib::getMeasurementUnits(uint8_t formula, uint8_t NWb, uint8_t MWb, char *str, size_t string_size)
{
  // If an invalid buffer was provided, return an invalid string.
  if (!str || !string_size)
  {
    return nullptr;
  }

  // Cannot retrieve units for measurements of type TEXT or UNKNOWN.
  if (getMeasurementType(formula) != VALUE)
  {
    return nullptr;
  }

  // This will point to one of the strings stored in from "units.h", which will be copied into the given string.
  const char *unit_pointer = nullptr;
  switch (formula)
  {
  case 0x01:
  case 0x74:
  case 0x80:
  case 0x8B:
    unit_pointer = KWP_units_RPM;
    break;

  case 0x02:
  case 0x14:
  case 0x17:
  case 0x21:
  case 0x77:
  case 0x81:
  case 0x93:
  case 0xA4:
  case 0xA8:
    unit_pointer = KWP_units_Percentage;
    break;

  case 0x03:
  case 0x09:
  case 0x1E:
  case 0x43:
  case 0x51:
  case 0x5B:
  case 0x70:
  case 0x78:
  case 0x84:
  case 0x8F:
  case 0x9A:
  case 0x9B:
  case 0xA2:
    unit_pointer = KWP_units_Angle;
    break;

  case 0x04:
    unit_pointer = (MWb < 127) ? KWP_units_Ignition_BTDC : KWP_units_Ignition_ATDC;
    break;

  case 0x05:
  case 0x1A:
  case 0x61:
  case 0x75:
  case 0x8C:
  case 0x95:
  case 0x9F:
    unit_pointer = KWP_units_Temperature;
    break;

  case 0x06:
  case 0x15:
  case 0x2B:
  case 0x42:
  case 0x48:
  case 0x4D:
  case 0x67:
  case 0x85:
  case 0x8A:
    unit_pointer = KWP_units_Voltage;
    break;

  case 0x07:
  case 0x6A:
  case 0x86:
  case 0x9E:
    unit_pointer = KWP_units_Speed;
    break;

  case 0x0C:
  case 0x40:
  case 0x49:
    unit_pointer = KWP_units_Resistance;
    break;

  case 0x0D:
  case 0x41:
  case 0x66:
    unit_pointer = KWP_units_Distance_m;
    break;

  case 0x0E:
  case 0x45:
  case 0x53:
  case 0x64:
  case 0xAF:
    unit_pointer = KWP_units_Pressure;
    break;

  case 0x0F:
  case 0x16:
  case 0x2F:
  case 0x89:
    unit_pointer = KWP_units_Time_m;
    break;

  case 0x12:
  case 0x32:
  case 0x60:
    unit_pointer = KWP_units_Pressure_m;
    break;

  case 0x13:
    unit_pointer = KWP_units_Volume;
    break;

  case 0x18:
  case 0x28:
  case 0x56:
  case 0x82:
    unit_pointer = KWP_units_Current;
    break;

  case 0x19:
  case 0x35:
  case 0x96:
    unit_pointer = KWP_units_Mass_Flow;
    break;

  case 0x1B:
    unit_pointer = (MWb < 128) ? KWP_units_Ignition_ATDC : KWP_units_Ignition_BTDC;
    break;

  case 0x22:
    unit_pointer = KWP_units_Correction;
    break;

  case 0x23:
  case 0x90:
    unit_pointer = KWP_units_Consumption_h;
    break;

  case 0x24:
  case 0x5C:
  case 0x6F:
    unit_pointer = KWP_units_Distance_k;
    break;

  case 0x26:
  case 0x2E:
  case 0x97:
    unit_pointer = KWP_units_Segment_Correction;
    break;

  case 0x27:
  case 0x31:
  case 0x33:
  case 0x98:
  case 0x99:
    unit_pointer = KWP_units_Mass_Per_Stroke_m;
    break;

  case 0x29:
    unit_pointer = KWP_units_Capacity;
    break;

  case 0x2A:
    unit_pointer = KWP_units_Power_k;
    break;

  case 0x2D:
  case 0x91:
    unit_pointer = KWP_units_Consumption_100km;
    break;

  case 0x34:
  case 0x5D:
  case 0x5E:
  case 0xB1:
    unit_pointer = KWP_units_Torque;
    break;

  case 0x37:
  case 0x3C:
  case 0x3E:
    unit_pointer = KWP_units_Time;
    break;

  case 0x3A:
  case 0x4E:
    unit_pointer = KWP_units_Misfires;
    break;

  case 0x44:
  case 0x55:
  case 0x57:
  case 0xA6:
  case 0xB2:
    unit_pointer = KWP_units_Turn_Rate;
    break;

  case 0x46:
  case 0x52:
  case 0x54:
    unit_pointer = KWP_units_Acceleration;
    break;

  case 0x47:
  case 0x9C:
  case 0x9D:
    unit_pointer = KWP_units_Distance_c;
    break;

  case 0x4A:
    unit_pointer = KWP_units_Time_mo;
    break;

  case 0x4C:
  case 0x50:
  case 0x58:
    unit_pointer = KWP_units_Resistance_k;
    break;

  case 0x59:
  case 0xA3:
    unit_pointer = KWP_units_Time_h;
    break;

  case 0x5A:
    unit_pointer = KWP_units_Mass_k;
    break;

  case 0x62:
    unit_pointer = KWP_units_Impulses;
    break;

  case 0x65:
    unit_pointer = KWP_units_Fuel_Level_Factor;
    break;

  case 0x68:
    unit_pointer = KWP_units_Volume_m;
    break;

  case 0x69:
  case 0x72:
    unit_pointer = KWP_units_Distance;
    break;

  case 0x73:
    unit_pointer = KWP_units_Power;
    break;

  case 0x7C:
  case 0xA5:
    unit_pointer = KWP_units_Current_m;
    break;

  case 0x7D:
    unit_pointer = KWP_units_Attenuation;
    break;

  case 0x83:
    unit_pointer = ((float(NWb) * float(MWb) / 2.0 - 30.0) < 0) ? KWP_units_Ignition_BTDC : KWP_units_Ignition_ATDC;
    break;

  case 0xA7:
    unit_pointer = KWP_units_Resistance_m;
    break;

  case 0xA9:
    unit_pointer = KWP_units_Voltage_m;
    break;

  case 0xAA:
  case 0xAB:
    unit_pointer = KWP_units_Mass;
    break;

  case 0xAC:
    unit_pointer = KWP_units_Mass_Flow_km;
    break;

  case 0xAD:
  case 0xB5:
    unit_pointer = KWP_units_Mass_Flow_m;
    break;

  case 0xAE:
    unit_pointer = KWP_units_Consumption_1000km;
    break;

  case 0xB0:
    unit_pointer = KWP_units_Parts_Per_Million;
    break;

  case 0xB4:
    unit_pointer = KWP_units_Mass_Per_Stroke_k;
    break;

  default:
    str[0] = '\0';
    return str;
  }

  strncpy(str, unit_pointer, string_size);
  str[string_size - 1] = '\0';
  return str;
}

/**
  Function:
    getMeasurementText(uint8_t measurement_index, uint8_t amount_of_measurements, uint8_t measurement_buffer[], size_t measurement_buffer_size, char str[], size_t string_size)
    getMeasurementText(uint8_t formula, uint8_t measurement_data[], uint8_t measurement_data_length, char str[], size_t string_size)

  Parameters (1):
    measurement_index       -> index of the measurement whose text must be determined (0-4)
    amount_of_measurements  -> total number of measurements stored in the array (value passed as reference to readGroup())
    measurement_buffer      -> array in which measurements have been stored by readGroup()
    measurement_buffer_size -> total size of the given array (provided with the sizeof() operator)
    str                     -> string (character array) into which to copy the text
    string_size             -> total size of the given array (provided with the sizeof() operator)

  Parameters (2):
    formula                 -> byte returned by getFormula()
    measurement_data        -> buffer returned by getMeasurementData()
    measurement_data_length -> byte returned by getMeasurementDataLength()
    str                     -> string (character array) into which to copy the text
    string_size             -> total size of the given array (provided with the sizeof() operator)

  Returns:
    char* -> the same character array provided (str)

  Description:
    Provides a string containing the text for a measurement of type TEXT.

  Notes:
    *It is a static function, so it does not require an instance to be used.
*/
char *KLineKWP1281Lib::getMeasurementText(uint8_t measurement_index, uint8_t amount_of_measurements, uint8_t *measurement_buffer, size_t measurement_buffer_size, char *str, size_t string_size)
{
  // Determine the formula.
  uint8_t formula = getFormula(measurement_index, amount_of_measurements, measurement_buffer, measurement_buffer_size);

  // Determine the measurement data and length.
  uint8_t *measurement_data = getMeasurementData(measurement_index, amount_of_measurements, measurement_buffer, measurement_buffer_size);
  uint8_t measurement_data_length = getMeasurementDataLength(measurement_index, amount_of_measurements, measurement_buffer, measurement_buffer_size);

  // Use the other function.
  return getMeasurementText(formula, measurement_data, measurement_data_length, str, string_size);
}

char *KLineKWP1281Lib::getMeasurementText(uint8_t formula, uint8_t *measurement_data, uint8_t measurement_data_length, char *str, size_t string_size)
{
  // If an invalid buffer was provided, return an invalid string.
  if (!measurement_data || !measurement_data_length || !str || !string_size)
  {
    return nullptr;
  }

  // Cannot retrieve text for measurements of type VALUE or UNKNOWN.
  if (getMeasurementType(formula) != TEXT)
  {
    return nullptr;
  }

  // Clear the string, so it's returned empty if the text cannot be determined.
  str[0] = '\0';

  // Handle formulas 5F, 76.
  if (formula == 0x5F || formula == 0x76)
  {
    switch (formula)
    {
    // ASCII long text
    case 0x5F:
    {
      // Copy the text to the string.
      size_t bytes_to_copy = (measurement_data_length < (string_size - 1) ? measurement_data_length : (string_size - 1));
      memcpy(str, measurement_data, bytes_to_copy);
      str[bytes_to_copy] = '\0';
    }
    break;

    // Hex bytes
    case 0x76:
    {
      // Determine how many hex bytes fit in the string (2 characters per byte).
      size_t max_hex_bytes_in_string = (string_size - 1) / 2;
      size_t bytes_to_copy = (measurement_data_length < max_hex_bytes_in_string) ? measurement_data_length : max_hex_bytes_in_string;

      // Add each hex byte to the string.
      for (size_t i = 0; i < bytes_to_copy; i++)
      {
        char hex_byte[3];
        sprintf(hex_byte, "%02X", measurement_data[i]);
        strcat(str, hex_byte);
      }
    }
    break;
    }

    return str;
  }
  // Handle all other formulas.
  else
  {
    // Get the two significant data bytes.
    uint8_t NWb = measurement_data[0], MWb = measurement_data[1];

    // This will point to one of the strings stored in from "units.h", which will be copied into the given string.
    const char *unit_pointer = nullptr;
    switch (formula)
    {
    // Warm/Cold
    case 0x0A:
      unit_pointer = MWb ? KWP_units_Warm : KWP_units_Cold;
      break;

    // Switch positions
    case 0x10:
    case 0x88:
      for (uint8_t i = 0; i < ((string_size < 8) ? string_size : 8); i++)
      {
        if (NWb & (1 << (7 - i)))
        {
          if (MWb & (1 << (7 - i)))
          {
            str[i] = '1';
          }
          else
          {
            str[i] = '0';
          }
        }
        else
        {
          str[i] = ' ';
        }
      }

      if (string_size > 8)
      {
        str[8] = '\0';
      }
      else
      {
        str[string_size - 1] = '\0';
      }
      return str;

    // 2 ASCII letters
    case 0x11:
    case 0x8E:
      if (string_size > 0)
      {
        str[0] = NWb;
      }

      if (string_size > 1)
      {
        str[1] = MWb;
      }

      if (string_size > 2)
      {
        str[2] = '\0';
      }
      else
      {
        str[string_size - 1] = '\0';
      }
      return str;

    // Map1/Map2
    case 0x1D:
      unit_pointer = (MWb < NWb) ? KWP_units_Map1 : KWP_units_Map2;
      break;

    // Text from table
    case 0x25:
    case 0x7B:
    {
#ifdef KWP1281_TEXT_TABLE_SUPPORTED

      // Construct the text code from the two bytes.
      uint16_t code = (NWb << 8) | MWb;

      // The text code will be used as the key for a binary search.
      struct keyed_struct bsearch_key;
      bsearch_key.code = code;

      // Binary search the key in the text table.
      struct keyed_struct *result = (struct keyed_struct *)bsearch(
          &bsearch_key, text_table_entries, ARRAYSIZE(text_table_entries),
          sizeof(struct keyed_struct), compare_keyed_structs);

      // If the text is found, copy it into the string.
      if (result)
      {
        // Copy the description string.
        strncpy(str, result->text, string_size);
        str[string_size - 1] = '\0';
      }
      // Otherwise, clear the string.
      else
      {
        str[0] = '\0';
      }

#else

      strncpy(str, "EN_f25", string_size);
      str[string_size - 1] = '\0';

#endif
    }
      return str;

    // Time
    case 0x2C:
      snprintf(str, string_size, "%02d:%02d", NWb, MWb);
      str[string_size - 1] = '\0';
      return str;

    // Hex bytes
    case 0x6B:
      snprintf(str, string_size, "%02X %02X", NWb, MWb);
      str[string_size - 1] = '\0';
      return str;

    // Date
    case 0x7F:
      snprintf(str, string_size, "%04d.%02d.%02d", 2000 + (MWb & 0x7F), ((NWb & 0x07) << 1) | ((MWb & 0x80) >> 7), (NWb & 0xF8) >> 3);
      str[string_size - 1] = '\0';
      return str;

    // Binary bytes
    case 0xA1:
      snprintf(str, string_size, BYTE_TO_BINARY_PATTERN " " BYTE_TO_BINARY_PATTERN, BYTE_TO_BINARY(NWb), BYTE_TO_BINARY(MWb));
      str[string_size - 1] = '\0';
      return str;

    // Unknown
    default:
      str[0] = '\0';
      return str;
    }

    strncpy(str, unit_pointer, string_size);
    str[string_size - 1] = '\0';
  }

  return str;
}

/**
  Function:
    getMeasurementTextLength(uint8_t measurement_index, uint8_t amount_of_measurements, uint8_t measurement_buffer[], size_t measurement_buffer_size)
    getMeasurementTextLength(uint8_t formula, uint8_t measurement_data[], uint8_t measurement_data_length)

  Parameters (1):
    measurement_index       -> index of the measurement whose text length must be determined (0-4)
    amount_of_measurements  -> total number of measurements stored in the array (value passed as reference to readGroup())
    measurement_buffer      -> array in which measurements have been stored by readGroup()
    measurement_buffer_size -> total size of the given array (provided with the sizeof() operator)

  Parameters (2):
    formula                 -> byte returned by getFormula()
    measurement_data        -> buffer returned by getMeasurementData()
    measurement_data_length -> byte returned by getMeasurementDataLength()

  Returns:
    size_t -> the length of the measurement's text

  Description:
    Provides the length of the text for a measurement of type TEXT.

  Notes:
    *It is a static function, so it does not require an instance to be used.
*/
size_t KLineKWP1281Lib::getMeasurementTextLength(uint8_t measurement_index, uint8_t amount_of_measurements, uint8_t *measurement_buffer, size_t measurement_buffer_size)
{
  // Determine the formula.
  uint8_t formula = getFormula(measurement_index, amount_of_measurements, measurement_buffer, measurement_buffer_size);

  // Determine the measurement data and length.
  uint8_t *measurement_data = getMeasurementData(measurement_index, amount_of_measurements, measurement_buffer, measurement_buffer_size);
  uint8_t measurement_data_length = getMeasurementDataLength(measurement_index, amount_of_measurements, measurement_buffer, measurement_buffer_size);

  // Use the other function.
  return getMeasurementTextLength(formula, measurement_data, measurement_data_length);
}

size_t KLineKWP1281Lib::getMeasurementTextLength(uint8_t formula, uint8_t *measurement_data, uint8_t measurement_data_length)
{
  // If an invalid buffer was provided, return 0.
  if (!measurement_data || !measurement_data_length)
  {
    return 0;
  }

  // Cannot retrieve text for measurements of type VALUE or UNKNOWN.
  if (getMeasurementType(formula) != TEXT)
  {
    return 0;
  }

  // Handle formulas 5F, 76.
  if (formula == 0x5F || formula == 0x76)
  {
    switch (formula)
    {
    // ASCII long text
    case 0x5F:
      // Each byte will take 1 character.
      return measurement_data_length;
      
    // Hex bytes
    case 0x76:
      // Each byte will take 2 characters.
      return 2 * measurement_data_length;
    }

    return 0;
  }
  // Handle all other formulas.
  else
  {
    // Get the two significant data bytes.
    uint8_t NWb = measurement_data[0], MWb = measurement_data[1];

    // This will point to one of the strings stored in from "units.h", which will be copied into the given string.
    const char *unit_pointer = nullptr;
    switch (formula)
    {
    // Warm/Cold
    case 0x0A:
      unit_pointer = MWb ? KWP_units_Warm : KWP_units_Cold;
      break;

    // Switch positions
    case 0x10:
    case 0x88:
      return 8;

    // 2 ASCII letters
    case 0x11:
    case 0x8E:
      return 2;

    // Map1/Map2
    case 0x1D:
      unit_pointer = (MWb < NWb) ? KWP_units_Map1 : KWP_units_Map2;
      break;

    // Text from table
    case 0x25:
    case 0x7B:
    {
#ifdef KWP1281_TEXT_TABLE_SUPPORTED

      // Construct the text code from the two bytes.
      uint16_t code = (NWb << 8) | MWb;

      // The text code will be used as the key for a binary search.
      struct keyed_struct bsearch_key;
      bsearch_key.code = code;

      // Binary search the key in the text table.
      struct keyed_struct *result = (struct keyed_struct *)bsearch(
          &bsearch_key, text_table_entries, ARRAYSIZE(text_table_entries),
          sizeof(struct keyed_struct), compare_keyed_structs);

      // If the text is found, return its length.
      if (result)
      {
        return strlen(result->text);
      }
      // Otherwise, return 0.
      else
      {
        return 0;
      }

#else

      return strlen("EN_f25");

#endif
    }
      return 0;

    // Time
    case 0x2C:
      return 5;

    // Hex bytes
    case 0x6B:
      return 5;

    // Date
    case 0x7F:
      return 10;

    // Binary bytes
    case 0xA1:
      return 17;

    // Unknown
    default:
      return 0;
    }

    return strlen(unit_pointer);
  }

  return 0;
}

/**
  Function:
    getMeasurementDecimals(uint8_t measurement_index, uint8_t amount_of_measurements, uint8_t measurement_buffer[], size_t measurement_buffer_size)
    getMeasurementDecimals(uint8_t formula)

  Parameters (1):
    measurement_index       -> index of the measurement whose recommended decimals must be determined (0-4)
    amount_of_measurements  -> total number of measurements stored in the array (value passed as reference to readGroup())
    measurement_buffer      -> array in which measurements have been stored by readGroup()
    measurement_buffer_size -> total size of the given array (provided with the sizeof() operator)

  Parameters (2):
    formula -> byte returned by getFormula()

  Returns:
    uint8_t -> recommended decimal places

  Description:
    Determines the recommended decimal places of a measurement from a buffer filled by readGroup().

  Notes:
    *It is a static function, so it does not require an instance to be used.
*/
uint8_t KLineKWP1281Lib::getMeasurementDecimals(uint8_t measurement_index, uint8_t amount_of_measurements, uint8_t *measurement_buffer, size_t measurement_buffer_size)
{
  // Determine the formula.
  uint8_t formula = getFormula(measurement_index, amount_of_measurements, measurement_buffer, measurement_buffer_size);

  // Use the other function.
  return getMeasurementDecimals(formula);
}

uint8_t KLineKWP1281Lib::getMeasurementDecimals(uint8_t formula)
{
  // Get the value from the table.
  if (formula < sizeof(KWP_decimals_table))
  {
    return KWP_decimals_table[formula];
  }

  return 0;
}

/**
  Function:
    getFormulaFromHeader(uint8_t measurement_index, uint8_t amount_of_measurements, uint8_t header_buffer[], size_t header_buffer_size)

  Parameters:
    measurement_index      -> index of the measurement whose formula must be determined (0-4)
    amount_of_measurements -> total number of measurements stored in the array (value passed as reference to readGroup())
    header_buffer          -> array in which a header has been stored by readGroup()
    header_buffer_size     -> total size of the given array (provided with the sizeof() operator)

  Returns:
    uint8_t -> formula byte

  Description:
    Retrieves the formula byte for a measurement from a buffer filled by readGroup() after it returned GROUP_HEADER.

  Notes:
    *Only use this function if readGroup() returned GROUP_HEADER.
    *It is a static function, so it does not require an instance to be used.
*/
uint8_t KLineKWP1281Lib::getFormulaFromHeader(uint8_t measurement_index, uint8_t amount_of_measurements, uint8_t *header_buffer, size_t header_buffer_size)
{
  // If the requested index is out of range (or the buffer doesn't contain measurements), return 0.
  if (measurement_index >= amount_of_measurements)
  {
    return 0;
  }

  // Go through each measurement template, returning the formula when on the requested index.
  size_t buffer_index = 0;
  for (uint8_t current_measurement = 0; current_measurement < amount_of_measurements && buffer_index < header_buffer_size; current_measurement++)
  {
    // Determine the length of the table attached to the template.
    uint8_t table_length = header_buffer[buffer_index + 2];
    
    // If currently on the requested index, return the formula.
    if (current_measurement == measurement_index)
    {
      return header_buffer[buffer_index];
    }
    
    // Skip over the FORMULA, NWB, TABLE_LEN, TABLE.
    buffer_index += (3 + table_length);
  }

  return 0;
}

/**
  Function:
    getNWbFromHeader(uint8_t measurement_index, uint8_t amount_of_measurements, uint8_t header_buffer[], size_t header_buffer_size)

  Parameters:
    measurement_index      -> index of the measurement whose NWb must be determined (0-4)
    amount_of_measurements -> total number of measurements stored in the array (value passed as reference to readGroup())
    header_buffer          -> array in which a header has been stored by readGroup()
    header_buffer_size     -> total size of the given array (provided with the sizeof() operator)

  Returns:
    uint8_t -> NWb (byte A)

  Description:
    Retrieves byte A for a measurement from a buffer filled by readGroup() after it returned GROUP_HEADER.

  Notes:
    *It is a static function, so it does not require an instance to be used.
*/
uint8_t KLineKWP1281Lib::getNWbFromHeader(uint8_t measurement_index, uint8_t amount_of_measurements, uint8_t *header_buffer, size_t header_buffer_size)
{
  // If the requested index is out of range (or the buffer doesn't contain measurements), return 0.
  if (measurement_index >= amount_of_measurements)
  {
    return 0;
  }

  // Go through each measurement template, returning NWb when on the requested index.
  size_t buffer_index = 0;
  for (uint8_t current_measurement = 0; current_measurement < amount_of_measurements && buffer_index < header_buffer_size; current_measurement++)
  {
    // Determine the length of the table attached to the template.
    uint8_t table_length = header_buffer[buffer_index + 2];
    
    // If currently on the requested index, return the NWb.
    if (current_measurement == measurement_index)
    {
      return header_buffer[buffer_index + 1];
    }
    
    // Skip over the FORMULA, NWB, TABLE_LEN, TABLE.
    buffer_index += (3 + table_length);
  }

  return 0;
}

/**
  Function:
    getMWbFromBody(uint8_t measurement_index, uint8_t amount_of_measurements, uint8_t body_buffer[], size_t body_buffer_size)

  Parameters:
    measurement_index      -> index of the measurement whose MWb must be determined (0-4)
    amount_of_measurements -> total number of measurements stored in the array (value passed as reference to readGroup())
    body_buffer            -> array in which a body has been stored by readGroup()
    body_buffer_size       -> total size of the given array (provided with the sizeof() operator)

  Returns:
    uint8_t -> MWb (byte B)

  Description:
    Retrieves byte B for a measurement from a buffer filled by readGroup() after it returned GROUP_BODY.

  Notes:
    *It is a static function, so it does not require an instance to be used.
*/
uint8_t KLineKWP1281Lib::getMWbFromBody(uint8_t measurement_index, uint8_t amount_of_measurements, uint8_t *body_buffer, size_t body_buffer_size)
{
  // If the requested index is out of range (or the buffer doesn't contain measurements), return 0.
  if (measurement_index >= amount_of_measurements || amount_of_measurements > body_buffer_size)
  {
    return 0;
  }

  // Return MWb.
  return body_buffer[measurement_index];
}

/**
  Function:
    getDataTableFromHeader(uint8_t measurement_index, uint8_t amount_of_measurements, uint8_t header_buffer[], size_t header_buffer_size)

  Parameters:
    measurement_index      -> index of the measurement whose data table must be retrieved (0-4)
    amount_of_measurements -> total number of measurements stored in the array (value passed as reference to readGroup())
    header_buffer          -> array in which a header has been stored by readGroup()
    header_buffer_size     -> total size of the given array (provided with the sizeof() operator)

  Returns:
    uint8_t* -> measurement data table buffer

  Description:
    Retrieves the data table for a measurement from a buffer filled by readGroup() after it returned GROUP_HEADER.

  Notes:
    *It is a static function, so it does not require an instance to be used.
*/
uint8_t *KLineKWP1281Lib::getDataTableFromHeader(uint8_t measurement_index, uint8_t amount_of_measurements, uint8_t *header_buffer, size_t header_buffer_size)
{
  // If the requested index is out of range (or the buffer doesn't contain measurements), return an invalid buffer.
  if (measurement_index >= amount_of_measurements)
  {
    return nullptr;
  }

  // Go through each measurement template, returning the data table when on the requested index.
  size_t buffer_index = 0;
  for (uint8_t current_measurement = 0; current_measurement < amount_of_measurements && buffer_index < header_buffer_size; current_measurement++)
  {
    // Determine the length of the table attached to the template.
    uint8_t table_length = header_buffer[buffer_index + 2];
    
    // If currently on the requested index, return the data table.
    if (current_measurement == measurement_index)
    {
      return &header_buffer[buffer_index + 3];
    }
    
    // Skip over the FORMULA, NWB, TABLE_LEN, TABLE.
    buffer_index += (3 + table_length);
  }

  return nullptr;
}

/**
  Function:
    getDataTableLengthFromHeader(uint8_t measurement_index, uint8_t amount_of_measurements, uint8_t header_buffer[], size_t header_buffer_size)

  Parameters:
    measurement_index      -> index of the measurement whose data table length must be determined (0-4)
    amount_of_measurements -> total number of measurements stored in the array (value passed as reference to readGroup())
    header_buffer          -> array in which a header has been stored by readGroup()
    header_buffer_size     -> total size of the given array (provided with the sizeof() operator)

  Returns:
    uint8_t -> measurement data table length

  Description:
    Retrieves the data table length for a measurement from a buffer filled by readGroup() after it returned GROUP_HEADER.

  Notes:
    *It is a static function, so it does not require an instance to be used.
*/
uint8_t KLineKWP1281Lib::getDataTableLengthFromHeader(uint8_t measurement_index, uint8_t amount_of_measurements, uint8_t *header_buffer, size_t header_buffer_size)
{
  // If the requested index is out of range (or the buffer doesn't contain measurements), return 0.
  if (measurement_index >= amount_of_measurements)
  {
    return 0;
  }

  // Go through each measurement template, returning the data table length when on the requested index.
  size_t buffer_index = 0;
  for (uint8_t current_measurement = 0; current_measurement < amount_of_measurements && buffer_index < header_buffer_size; current_measurement++)
  {
    // Determine the length of the table attached to the template.
    uint8_t table_length = header_buffer[buffer_index + 2];
    
    // If currently on the requested index, return the data table length.
    if (current_measurement == measurement_index)
    {
      return header_buffer[buffer_index + 2];
    }
    
    // Skip over the FORMULA, NWB, TABLE_LEN, TABLE.
    buffer_index += (3 + table_length);
  }

  return 0;
}

/**
  Function:
    getMeasurementTypeFromHeader(uint8_t measurement_index, uint8_t amount_of_measurements, uint8_t header_buffer[], size_t header_buffer_size)
    getMeasurementTypeFromHeader(uint8_t formula)

  Parameters (1):
    measurement_index      -> index of the measurement whose type must be determined (0-4)
    amount_of_measurements -> total number of measurements stored in the array (value passed as reference to readGroup())
    header_buffer          -> array in which a header has been stored by readGroup()
    header_buffer_size     -> total size of the given array (provided with the sizeof() operator)

  Parameters (2):
    formula -> byte returned by getFormulaFromHeader()

  Returns:
    measurementType ->
      UNKNOWN - requested measurement outside range
      VALUE   - get value with getMeasurementValueFromHeaderBody() and units with getMeasurementUnitsFromHeaderBody()
      TEXT    - get text with getMeasurementTextFromHeaderBody() and, sometimes, original value with getMeasurementValueFromHeaderBody()

  Description:
    Determines the type of a measurement from a buffer filled by readGroup() after it returned GROUP_HEADER.

  Notes:
    *If an invalid measurement_index is specified, the returned type is UNKNOWN.
    *Even if the measurement is of type TEXT, its value might contain the "origin" of the string, like a code or 16-bit value so it can be used without
    necessarily checking the text itself.
    *For example, for a clock measurement, the text string will contain "hh:mm", but the value will also be hh.mm (hours before decimal, minutes after).
    *It is a static function, so it does not require an instance to be used.
*/
KLineKWP1281Lib::measurementType KLineKWP1281Lib::getMeasurementTypeFromHeader(uint8_t measurement_index, uint8_t amount_of_measurements, uint8_t *header_buffer, size_t header_buffer_size)
{
  // Determine the formula.
  uint8_t formula = getFormulaFromHeader(measurement_index, amount_of_measurements, header_buffer, header_buffer_size);

  // Use the other function.
  return getMeasurementTypeFromHeader(formula);
}

KLineKWP1281Lib::measurementType KLineKWP1281Lib::getMeasurementTypeFromHeader(uint8_t formula)
{
  // There are no valid formulas after B5.
  if (formula > 0xB5)
  {
    return UNKNOWN;
  }
  
  // Only a few measurement formulas are of the "TEXT" type.
  switch (formula)
  {
  case 0x00: // N/A
  case 0x3F: // ASCII long text with unknown length (is converted to 5F by readGroup())
  case 0x6C: // Environment
  case 0x6D: // N/A
  case 0x6E: // Workshop
  // In header+body mode, "long" formulas are forbidden.
  case 0x5F: // ASCII long text with known length
  case 0x76: // Hex bytes with known length
  case 0xA0: // Variable units
    return UNKNOWN;

  case 0x0A: // Warm/Cold
  case 0x10: // Switch positions
  case 0x88: // Switch positions
  case 0x11: // 2 ASCII letters
  case 0x8E: // 2 ASCII letters
  case 0x1D: // Map1/Map2
  case 0x25: // Text from table
  case 0x7B: // Text from table
  case 0x2C: // Time
  case 0x6B: // Hex bytes
  case 0x7F: // Date
  case 0xA1: // Binary bytes
  case 0x8D: // ASCII string
    return TEXT;

  default:
    return VALUE;
  }
}

/**
  Function:
    getMeasurementValueFromHeaderBody(uint8_t measurement_index, uint8_t amount_of_measurements_in_header, uint8_t header_buffer[], size_t header_buffer_size, uint8_t amount_of_measurements_in_body, uint8_t body_buffer[], size_t body_buffer_size)
    getMeasurementValueFromHeaderBody(uint8_t formula, uint8_t NWb, uint8_t MWb, uint8_t data_table[], uint8_t data_table_length)

  Parameters (1):
    measurement_index                -> index of the measurement that needs to be calculated (0-3)
    amount_of_measurements_in_header -> total number of measurements stored in the header array (value passed as reference to readGroup(), returned GROUP_HEADER)
    header_buffer                    -> array in which a header has been stored by readGroup() (returned GROUP_HEADER)
    header_buffer_size               -> total size of the given header array (provided with the sizeof() operator)
    amount_of_measurements_in_body   -> total number of measurements stored in the body array (value passed as reference to readGroup(), returned GROUP_BODY)
    body_buffer                      -> array in which a body has been stored by readGroup() (returned GROUP_BODY)
    body_buffer_size                 -> total size of the given body array (provided with the sizeof() operator)

  Parameters (2):
    formula           -> byte returned by getFormulaFromHeader()
    NWb               -> byte returned by getNWbFromHeader()
    MWb               -> byte returned by getMWbFromBody()
    data_table        -> buffer returned by getDataTableFromHeader()
    data_table_length -> byte returned by getDataTableLengthFromHeader()
  
  Returns:
    double -> calculated value

  Description:
    Calculates the actual value of a measurement from two buffers filled by readGroup() after it returned GROUP_HEADER and GROUP_BODY.

  Notes:
    *If an invalid measurement_index is specified, the returned value is nan.
    *It is a static function, so it does not require an instance to be used.
*/
double KLineKWP1281Lib::getMeasurementValueFromHeaderBody(uint8_t measurement_index, uint8_t amount_of_measurements_in_header, uint8_t *header_buffer, size_t header_buffer_size, uint8_t amount_of_measurements_in_body, uint8_t *body_buffer, size_t body_buffer_size)
{
  // Determine the formula.
  uint8_t formula = getFormulaFromHeader(measurement_index, amount_of_measurements_in_header, header_buffer, header_buffer_size);
    
  // Retrieve NWb and MWb.
  uint8_t NWb = getNWbFromHeader(measurement_index, amount_of_measurements_in_header, header_buffer, header_buffer_size);
  uint8_t MWb = getMWbFromBody(measurement_index, amount_of_measurements_in_body, body_buffer, body_buffer_size);
  
  // Retrieve the data table and its length.
  uint8_t *data_table = getDataTableFromHeader(measurement_index, amount_of_measurements_in_header, header_buffer, header_buffer_size);
  uint8_t data_table_length = getDataTableLengthFromHeader(measurement_index, amount_of_measurements_in_header, header_buffer, header_buffer_size);

  // Use the other function.
  return getMeasurementValueFromHeaderBody(formula, NWb, MWb, data_table, data_table_length);
}

double KLineKWP1281Lib::getMeasurementValueFromHeaderBody(uint8_t formula, uint8_t NWb, uint8_t MWb, uint8_t *data_table, uint8_t data_table_length)
{  
  // In header+body mode, "long" formulas are forbidden.
  if (formula == 0x3F || formula == 0x5F || formula == 0x76 || formula == 0x3F || formula == 0xA0)
  {
    return 0.0 / 0.0;
  }
  
  // Formulas 8B, 8C, 8D and 93 are specific to header+body responses, because they require a table that is provided in the header.
  // For formulas 8B (RPM), 8C (Temperature), 8D (Strings) and 93 (Inclination), the table length should not be 0.
  // For all of them except formula 8D, the table length should be 17.
  if (((formula == 0x8B || formula == 0x8C || formula == 0x93) && (!data_table || data_table_length != 17)) || (formula == 0x8D && (!data_table || data_table_length == 0)))
  {
    return 0.0 / 0.0;
  }
  
  // Formulas 8B, 8C and 93 are very similar.
  if (formula == 0x8B || formula == 0x8C || formula == 0x93)
  {
    // Calculate the index into the data table (map table), based on the value received in the body response.
    uint8_t map_table_index = MWb / (data_table_length - 1);
    
    // In theory, the length of the map table should always be 17.
    // Being a byte, the maximum value of MWb is 255.
    // So, the map_table_index should be at most 255 / 16 = 15.
    // The next byte will also be needed, so index 16 would be accessed as well, which is still fine.
    // This means the array should never be accessed out-of-bounds, but I still choose to constrain it, just in case.
    if (map_table_index > data_table_length - 2)
    {
      map_table_index = data_table_length - 2;
    }
    
    // Retrieve the appropriate map byte from the table.
    uint8_t map_byte = data_table[map_table_index];
    
    // Calculate the difference between the next byte and this one.
    int16_t difference = data_table[map_table_index + 1] - map_byte;
    
    // Calculate the value.
    double result = map_byte + ((difference * (MWb % (data_table_length - 1))) / (data_table_length - 1));
    
    // For formula 8B, the result is multiplied by NWb.
    if (formula == 0x8B)
    {
      return result * NWb;
    }
    
    // For formulas 8C and 93, NWb is subtracted from the result.
    return result - NWb;
  }
    
  // For all other formulas, use the standard function.
  return getMeasurementValue(formula, NWb, MWb);
}

/**
  Function:
    getMeasurementUnitsFromHeaderBody(uint8_t measurement_index, uint8_t amount_of_measurements_in_header, uint8_t header_buffer[], size_t header_buffer_size, uint8_t amount_of_measurements_in_body, uint8_t body_buffer[], size_t body_buffer_size, char str[], size_t string_size)
    getMeasurementUnitsFromHeaderBody(uint8_t formula, uint8_t NWb, uint8_t MWb, char str[], size_t string_size)

  Parameters (1):
    measurement_index                -> index of the measurement whose units must be determined (0-3)
    amount_of_measurements_in_header -> total number of measurements stored in the header array (value passed as reference to readGroup(), returned GROUP_HEADER)
    header_buffer                    -> array in which a header has been stored by readGroup() (returned GROUP_HEADER)
    header_buffer_size               -> total size of the given header array (provided with the sizeof() operator)
    amount_of_measurements_in_body   -> total number of measurements stored in the body array (value passed as reference to readGroup(), returned GROUP_BODY)
    body_buffer                      -> array in which a body has been stored by readGroup() (returned GROUP_BODY)
    body_buffer_size                 -> total size of the given body array (provided with the sizeof() operator)
    str                              -> string (character array) into which to copy the units
    string_size                      -> total size of the given array (provided with the sizeof() operator)

  Parameters (2):
    formula           -> byte returned by getFormulaFromHeader()
    NWb               -> byte returned by getNWbFromHeader()
    MWb               -> byte returned by getMWbFromBody()
    str               -> string (character array) into which to copy the units
    string_size       -> total size of the given array (provided with the sizeof() operator)

  Returns:
    char* -> the same character array provided (str)

  Description:
    Provides a string containing the proper units for a measurement of type VALUE from two buffers filled by readGroup() after it returned GROUP_HEADER and GROUP_BODY.

  Notes:
    *It is a static function, so it does not require an instance to be used.
*/
char *KLineKWP1281Lib::getMeasurementUnitsFromHeaderBody(uint8_t measurement_index, uint8_t amount_of_measurements_in_header, uint8_t *header_buffer, size_t header_buffer_size, uint8_t amount_of_measurements_in_body, uint8_t *body_buffer, size_t body_buffer_size, char *str, size_t string_size)
{
  // Determine the formula.
  uint8_t formula = getFormulaFromHeader(measurement_index, amount_of_measurements_in_header, header_buffer, header_buffer_size);
  
  // Retrieve NWb and MWb.
  uint8_t NWb = getNWbFromHeader(measurement_index, amount_of_measurements_in_header, header_buffer, header_buffer_size);
  uint8_t MWb = getMWbFromBody(measurement_index, amount_of_measurements_in_body, body_buffer, body_buffer_size);

  // Use the other function.
  return getMeasurementUnitsFromHeaderBody(formula, NWb, MWb, str, string_size);
}

char *KLineKWP1281Lib::getMeasurementUnitsFromHeaderBody(uint8_t formula, uint8_t NWb, uint8_t MWb, char* str, size_t string_size)
{
  // Use the standard function.
  return getMeasurementUnits(formula, NWb, MWb, str, string_size);
}

/**
  Function:
    getMeasurementTextFromHeaderBody(uint8_t measurement_index, uint8_t amount_of_measurements_in_header, uint8_t header_buffer[], size_t header_buffer_size, uint8_t amount_of_measurements_in_body, uint8_t body_buffer[], size_t body_buffer_size, char str[], size_t string_size)
    getMeasurementTextFromHeaderBody(uint8_t formula, uint8_t NWb, uint8_t MWb, uint8_t data_table[], uint8_t data_table_length, char str[], size_t string_size)

  Parameters (1):
    measurement_index                -> index of the measurement whose text must be determined (0-3)
    amount_of_measurements_in_header -> total number of measurements stored in the header array (value passed as reference to readGroup(), returned GROUP_HEADER)
    header_buffer                    -> array in which a header has been stored by readGroup() (returned GROUP_HEADER)
    header_buffer_size               -> total size of the given header array (provided with the sizeof() operator)
    amount_of_measurements_in_body   -> total number of measurements stored in the body array (value passed as reference to readGroup(), returned GROUP_BODY)
    body_buffer                      -> array in which a body has been stored by readGroup() (returned GROUP_BODY)
    body_buffer_size                 -> total size of the given body array (provided with the sizeof() operator)
    str                              -> string (character array) into which to copy the text
    string_size                      -> total size of the given array (provided with the sizeof() operator)

  Parameters (2):
    formula           -> byte returned by getFormulaFromHeader()
    NWb               -> byte returned by getNWbFromHeader()
    MWb               -> byte returned by getMWbFromBody()
    data_table        -> buffer returned by getDataTableFromHeader()
    data_table_length -> byte returned by getDataTableLengthFromHeader()
    str               -> string (character array) into which to copy the text
    string_size       -> total size of the given array (provided with the sizeof() operator)
    
  Returns:
    char* -> the same character array provided (str)

  Description:
    Provides a string containing the text for a measurement of type TEXT from two buffers filled by readGroup() after it returned GROUP_HEADER and GROUP_BODY..

  Notes:
    *It is a static function, so it does not require an instance to be used.
*/
char *KLineKWP1281Lib::getMeasurementTextFromHeaderBody(uint8_t measurement_index, uint8_t amount_of_measurements_in_header, uint8_t *header_buffer, size_t header_buffer_size, uint8_t amount_of_measurements_in_body, uint8_t *body_buffer, size_t body_buffer_size, char* str, size_t string_size)
{
  // Determine the formula.
  uint8_t formula = getFormulaFromHeader(measurement_index, amount_of_measurements_in_header, header_buffer, header_buffer_size);
  
  // Retrieve NWb and MWb.
  uint8_t NWb = getNWbFromHeader(measurement_index, amount_of_measurements_in_header, header_buffer, header_buffer_size);
  uint8_t MWb = getMWbFromBody(measurement_index, amount_of_measurements_in_body, body_buffer, body_buffer_size);

  // Retrieve the data table and its length.
  uint8_t *data_table = getDataTableFromHeader(measurement_index, amount_of_measurements_in_header, header_buffer, header_buffer_size);
  uint8_t data_table_length = getDataTableLengthFromHeader(measurement_index, amount_of_measurements_in_header, header_buffer, header_buffer_size);

  // Use the other function.
  return getMeasurementTextFromHeaderBody(formula, NWb, MWb, data_table, data_table_length, str, string_size);
}

char *KLineKWP1281Lib::getMeasurementTextFromHeaderBody(uint8_t formula, uint8_t NWb, uint8_t MWb, uint8_t *data_table, uint8_t data_table_length, char* str, size_t string_size)
{
  // If an invalid buffer was provided, return an invalid string.
  if (!str || !string_size)
  {
    return nullptr;
  }
  
  // In header+body mode, "long" formulas are forbidden.
  if (formula == 0x3F || formula == 0x5F || formula == 0x76 || formula == 0x3F || formula == 0xA0)
  {
    return nullptr;
  }
  
  // The table length for formula 8D should be non-zero.
  if (formula == 0x8D && (!data_table || data_table_length == 0))
  {
    return nullptr;
  }
  
  // The table for formula 8D contains a list of ASCII strings, delimited by 0x03.
  // BYTE_B is used as an index into that list, and the string that is found there is displayed.
  if (formula == 0x8D)
  {
    uint8_t start_index = 0, end_index = 0;
    uint8_t string_counter = 0;
    
    // Iterate through each character in the response.
    for (uint8_t i = 0; i < data_table_length; i++)
    {
      // If a terminator is found, check if this was the requested string.
      if (data_table[i] == 0x03)
      {
        // The end of this string is on the terminator.
        end_index = i;
        
        // If this was the requested string, return it.
        if (string_counter == MWb)
        {
          // Calculate the string's length.
          uint8_t string_length = end_index - start_index;
          
          // Determine how many characters to copy.
          uint8_t bytes_to_copy = (string_length < (string_size - 1)) ? string_length : (string_size - 1);
          
          // Copy the string.
          memcpy(str, &data_table[start_index], bytes_to_copy);
          str[bytes_to_copy] = '\0';
          return str;
        }
        // Otherwise, set the next string to start after the terminator.
        else
        {
          start_index = i + 1;
        }
        
        // Count the string.
        string_counter++;
      }
    }
    
    // If the string's end is before its start, it means the last string wasn't terminated.
    // If the last string was the one requested, copy it.
    if ((end_index < start_index) && (MWb == string_counter))
    {
      // Calculate the string's length.
      uint8_t string_length = data_table_length - start_index;
      
      // Determine how many characters to copy.
      uint8_t bytes_to_copy = (string_length < (string_size - 1)) ? string_length : (string_size - 1);
          
      // Copy the string.
      memcpy(str, &data_table[start_index], bytes_to_copy);
      str[bytes_to_copy] = '\0';
      return str;
    }
    
    // If the requested string was not found, return an empty string.
    str[0] = '\0';
    return str;
  }
  
  // For all other formulas, use the standard function.
  uint8_t dummy_measurement_data[] = {NWb, MWb};
  uint8_t dummy_measurement_data_length = 2;
  return getMeasurementText(formula, dummy_measurement_data, dummy_measurement_data_length, str, string_size);
}

/**
  Function:
    getMeasurementTextLengthFromHeaderBody(uint8_t measurement_index, uint8_t amount_of_measurements_in_header, uint8_t header_buffer[], size_t header_buffer_size, uint8_t amount_of_measurements_in_body, uint8_t body_buffer[], size_t body_buffer_size)

  Parameters (1):
    measurement_index                -> index of the measurement whose text length must be determined (0-3)
    amount_of_measurements_in_header -> total number of measurements stored in the header array (value passed as reference to readGroup(), returned GROUP_HEADER)
    header_buffer                    -> array in which a header has been stored by readGroup() (returned GROUP_HEADER)
    header_buffer_size               -> total size of the given header array (provided with the sizeof() operator)
    amount_of_measurements_in_body   -> total number of measurements stored in the body array (value passed as reference to readGroup(), returned GROUP_BODY)
    body_buffer                      -> array in which a body has been stored by readGroup() (returned GROUP_BODY)
    body_buffer_size                 -> total size of the given body array (provided with the sizeof() operator)

  Parameters (2):
    formula           -> byte returned by getFormulaFromHeader()
    NWb               -> byte returned by getNWbFromHeader()
    MWb               -> byte returned by getMWbFromBody()
    data_table        -> buffer returned by getDataTableFromHeader()
    data_table_length -> byte returned by getDataTableLengthFromHeader()
    
  Returns:
    size_t -> the length of the measurement's text

  Description:
    Provides the length of the text for a measurement of type TEXT from two buffers filled by readGroup() after it returned GROUP_HEADER and GROUP_BODY.

  Notes:
    *It is a static function, so it does not require an instance to be used.
*/
size_t KLineKWP1281Lib::getMeasurementTextLengthFromHeaderBody(uint8_t measurement_index, uint8_t amount_of_measurements_in_header, uint8_t *header_buffer, size_t header_buffer_size, uint8_t amount_of_measurements_in_body, uint8_t *body_buffer, size_t body_buffer_size)
{
  // Determine the formula.
  uint8_t formula = getFormulaFromHeader(measurement_index, amount_of_measurements_in_header, header_buffer, header_buffer_size);
  
  // Retrieve NWb and MWb.
  uint8_t NWb = getNWbFromHeader(measurement_index, amount_of_measurements_in_header, header_buffer, header_buffer_size);
  uint8_t MWb = getMWbFromBody(measurement_index, amount_of_measurements_in_body, body_buffer, body_buffer_size);

  // Retrieve the data table and its length.
  uint8_t *data_table = getDataTableFromHeader(measurement_index, amount_of_measurements_in_header, header_buffer, header_buffer_size);
  uint8_t data_table_length = getDataTableLengthFromHeader(measurement_index, amount_of_measurements_in_header, header_buffer, header_buffer_size);

  // Use the other function.
  return getMeasurementTextLengthFromHeaderBody(formula, NWb, MWb, data_table, data_table_length);
}

size_t KLineKWP1281Lib::getMeasurementTextLengthFromHeaderBody(uint8_t formula, uint8_t NWb, uint8_t MWb, uint8_t *data_table, uint8_t data_table_length)
{
  // In header+body mode, "long" formulas are forbidden.
  if (formula == 0x3F || formula == 0x5F || formula == 0x76 || formula == 0x3F || formula == 0xA0)
  {
    return 0;
  }
  
  // The table length for formula 8D should be non-zero.
  if (formula == 0x8D && (!data_table || data_table_length == 0))
  {
    return 0;
  }
  
  // The table for formula 8D contains a list of ASCII strings, delimited by 0x03.
  // BYTE_B is used as an index into that list, and the string that is found there is displayed.
  if (formula == 0x8D)
  {
    uint8_t start_index = 0, end_index = 0;
    uint8_t string_counter = 0;
    
    // Iterate through each character in the response.
    for (uint8_t i = 0; i < data_table_length; i++)
    {
      // If a terminator is found, check if this was the requested string.
      if (data_table[i] == 0x03)
      {
        // The end of this string is on the terminator.
        end_index = i;
        
        // If this was the requested string, return its length.
        if (string_counter == MWb)
        {
          return end_index - start_index;
        }
        // Otherwise, set the next string to start after the terminator.
        else
        {
          start_index = i + 1;
        }
        
        // Count the string.
        string_counter++;
      }
    }
    
    // If the string's end is before its start, it means the last string wasn't terminated.
    // If the last string was the one requested, return its length.
    if ((end_index < start_index) && (MWb == string_counter))
    {
      return data_table_length - start_index;
    }
    
    // If the requested string was not found, return 0.
    return 0;
  }
  
  // For all other formulas, use the standard function.
  uint8_t dummy_measurement_data[] = {NWb, MWb};
  uint8_t dummy_measurement_data_length = 2;
  return getMeasurementTextLength(formula, dummy_measurement_data, dummy_measurement_data_length);
}

/**
  Function:
    getMeasurementDecimalsFromHeader(uint8_t measurement_index, uint8_t amount_of_measurements, uint8_t header_buffer[], size_t header_buffer_size)
    getMeasurementDecimalsFromHeader(uint8_t formula)

  Parameters (1):
    measurement_index      -> index of the measurement whose recommended decimals must be determined (0-4)
    amount_of_measurements -> total number of measurements stored in the array (value passed as reference to readGroup())
    header_buffer          -> array in which a header has been stored by readGroup()
    header_buffer_size     -> total size of the given array (provided with the sizeof() operator)

  Parameters (2):
    formula -> byte returned by getFormulaFromHeader()
    
  Returns:
    uint8_t -> recommended decimal places

  Description:
    Determines the recommended decimal places of a measurement from a buffer filled by readGroup() after it returned GROUP_HEADER.

  Notes:
    *It is a static function, so it does not require an instance to be used.
*/
uint8_t KLineKWP1281Lib::getMeasurementDecimalsFromHeader(uint8_t measurement_index, uint8_t amount_of_measurements, uint8_t *header_buffer, size_t header_buffer_size)
{
  // Determine the formula.
  uint8_t formula = getFormulaFromHeader(measurement_index, amount_of_measurements, header_buffer, header_buffer_size);

  // Use the other function.
  return getMeasurementDecimalsFromHeader(formula);
}

uint8_t KLineKWP1281Lib::getMeasurementDecimalsFromHeader(uint8_t formula)
{
  // Get the value from the table.
  if (formula < sizeof(KWP_decimals_table))
  {
    return KWP_decimals_table[formula];
  }

  return 0;
}

/**
  Function:
    readROM(uint8_t chunk_size, uint16_t start_address, size_t &bytes_received, uint8_t memory_buffer[], uint8_t memory_buffer_size)

  Parameters:
    chunk_size         -> how many bytes to read
    start_address      -> address from which to start reading
    bytes_received     -> how many bytes were read
    memory_buffer      -> array into which to store the bytes read
    memory_buffer_size -> total (maximum) length of the given array

  Returns:
    executionStatus -> whether or not the operation executed successfully
       SUCCESS
       FAIL
       ERROR

  Description:
    Reads a chunk of ROM/EEPROM.
*/
KLineKWP1281Lib::executionStatus KLineKWP1281Lib::readROM(uint8_t chunk_size, uint16_t start_address, size_t &bytes_received, uint8_t *memory_buffer, uint8_t memory_buffer_size)
{
  uint8_t start_address_high_byte = start_address >> 8;
  uint8_t start_address_low_byte = start_address & 0xFF;

  uint8_t parameters[] = {chunk_size, start_address_high_byte, start_address_low_byte};
  if (!send_message(KWP_REQUEST_READ_ROM, parameters, sizeof(parameters)))
  {
    show_debug_info(SEND_ERROR);
    return ERROR;
  }

  switch (receive_message(&bytes_received, memory_buffer, memory_buffer_size, responseTimeout))
  {
  case TYPE_ACK:
  case TYPE_REFUSE:
    show_debug_info(READ_ROM_NOT_SUPPORTED);
    return FAIL;

  case TYPE_ROM:
    show_debug_info(RECEIVED_ROM);
    return SUCCESS;

  default:
    show_debug_info(UNEXPECTED_RESPONSE);
    return ERROR;
  }
}

/**
  Function:
    outputTests(uint16_t &current_output_test)

  Parameters:
    current_output_test -> will contain the currently running output test ID

  Returns:
    executionStatus -> whether or not the operation executed successfully
       SUCCESS
       FAIL
       ERROR

  Description:
    Performs an output test.

  Notes:
    *Output tests are always performed in a sequence that cannot be changed.
    *To go through them all, use multiple calls of outputTests() until FAIL is returned.
    *The currently running test ID is stored in current_output_test.
    *Get a description of the currently running test with the getOutputTestDescription() function.
*/
KLineKWP1281Lib::executionStatus KLineKWP1281Lib::outputTests(uint16_t &current_output_test)
{
  uint8_t parameters[] = {0x00};
  send_message(KWP_REQUEST_OUTPUT_TEST, parameters, sizeof(parameters));

  size_t bytes_received;
  switch (receive_message(&bytes_received, receive_buffer, sizeof(receive_buffer), responseTimeout))
  {
  case TYPE_REFUSE:
    show_debug_info(OUTPUT_TESTS_NOT_SUPPORTED);
    return FAIL;

  case TYPE_OUTPUT_TEST:
    show_debug_info(RECEIVED_OUTPUT_TEST);
    current_output_test = (receive_buffer[0] << 8) | receive_buffer[1];
    return SUCCESS;

  case TYPE_ACK:
    show_debug_info(END_OF_OUTPUT_TESTS);
    current_output_test = 0;
    return FAIL;

  default:
    show_debug_info(UNEXPECTED_RESPONSE);
    return ERROR;
  }
}

/**
  Function:
    getOutputTestDescription(uint16_t output_test, char str[], size_t string_size)

  Parameters:
    output_test -> output test ID, given by outputTests()
    str         -> string (character array) into which to copy the description string
    string_size -> total size of the given array (provided with the sizeof() operator)

  Returns:
    char* -> the same character array provided (str)

  Description:
    Provides the description string for an output test ID given by outputTests().

  Notes:
    *It is a static function, so it does not require an instance to be used.
*/
char *KLineKWP1281Lib::getOutputTestDescription(uint16_t output_test, char *str, size_t string_size)
{
  // The output test ID is technically just a fault code.
  return getFaultDescription(output_test, str, string_size);
}

/**
  Function:
    getOutputTestDescriptionLength(uint16_t output_test)

  Parameters:
    output_test -> output test ID, given by outputTests()

  Returns:
    size_t -> string length

  Description:
    Provides the length of the description string for an output test ID given by outputTests().

  Notes:
    *It is a static function, so it does not require an instance to be used.
*/
size_t KLineKWP1281Lib::getOutputTestDescriptionLength(uint16_t output_test)
{
  // The output test ID is technically just a fault code.
  return getFaultDescriptionLength(output_test);
}

/// PRIVATE

/**
  Function:
    bitbang_5baud(uint8_t module)

  Parameters:
    module -> logical ID of the target module (engine-0x01, cluster-0x17 etc.)

  Description:
    Initializes the requested module.
*/
void KLineKWP1281Lib::bitbang_5baud(uint8_t module)
{
  // Stop serial communication on the K-line.
  _endFunction();

  // Configure the transmit pin for sending the 5-baud initialization manually.
  pinMode(_tx_pin, OUTPUT);

  // Construct the wake-up byte, bit by bit.
  bool bits[10], parity = 1;
  for (uint8_t i = 0; i < sizeof(bits); i++)
  {
    switch (i)
    {
    case 0: // start bit
      bits[i] = 0;
      break;

    default:                             // 7 data bits
      bits[i] = module & (1 << (i - 1)); // address of module, LSB first
      parity ^= bits[i];                 // XOR to calculate parity
      break;

    case 8: // odd parity bit
      bits[i] = parity;
      break;

    case 9: // stop bit
      bits[i] = 1;
      break;
    }
  }

  // Send the constructed byte by sending each bit from the array.
  K_LOG_INFO("5-baud-init");
  for (uint8_t i = 0; i < sizeof(bits); i++)
  {
    // Set the transmit line according to the current bit.
    digitalWrite(_tx_pin, bits[i]);

    // Wait 200ms after each bit for a speed of 5 baud.
    vTaskDelay(pdMS_TO_TICKS(200));
  }
}

/**
  Function:
    read_identification(bool request_extra_identification)

  Parameters:
    request_extra_identification -> whether or not to request extra identification if it is available

  Default parameters:
    request_extra_identification = true

  Returns:
    executionStatus -> whether or not the operation executed successfully
       SUCCESS
       FAIL
       ERROR

  Description:
    Receives the module's identification and stores it in the private struct's strings.
*/
KLineKWP1281Lib::executionStatus KLineKWP1281Lib::read_identification(bool request_extra_identification)
{
  // 1. VAG part number
  size_t bytes_received;
  RETURN_TYPE ret_val = receive_message(&bytes_received, receive_buffer, sizeof(receive_buffer), responseTimeout);

  // In the receive_message() function above, if the module has just been initialized, it may
  // send the protocol identification bytes again.
  // After that, it will not happen again, so reset this flag.
  may_send_protocol_parameters_again = false;

  switch (ret_val)
  {
  case TYPE_ACK:
    show_debug_info(NO_PART_NUMBER_AVAILABLE);
    return SUCCESS;

  case TYPE_ID:
    show_debug_info(RECEIVED_PART_NUMBER);
    break;

  default:
    show_debug_info(UNEXPECTED_RESPONSE);
    return ERROR;
  }

  // The top bit in the first byte indicates whether or not there is extra identification available.
  bool extra_identification_available = false;
  if (receive_buffer[0] & 0x80)
  {
    extra_identification_available = true;

    // Strip that bit away, leaving only the 7-bit ASCII character.
    receive_buffer[0] &= ~(1 << 7);
  }

  // Copy the string from the buffer to the struct.
  memset(identification_data.part_number, 0, sizeof(identification_data.part_number));
  memcpy(identification_data.part_number, receive_buffer, bytes_received);

  // Confirm receiving of the message and request the next.
  if (!acknowledge())
  {
    show_debug_info(SEND_ERROR);
    return ERROR;
  }

  // 2. Regular identification
  // Part 1
  switch (receive_message(&bytes_received, receive_buffer, sizeof(receive_buffer), responseTimeout))
  {
  case TYPE_ACK:
    show_debug_info(NO_ID_PART1_AVAILABLE);
    return SUCCESS;

  case TYPE_ID:
    show_debug_info(RECEIVED_ID_PART1);
    break;

  default:
    show_debug_info(UNEXPECTED_RESPONSE);
    return ERROR;
  }

  // Copy the string from the buffer to the struct.
  memset(identification_data.identification, 0, sizeof(identification_data.identification));
  memcpy(identification_data.identification, receive_buffer, bytes_received);
  size_t identification_string_index = bytes_received;

  // Confirm receiving of the message and request the next.
  if (!acknowledge())
  {
    show_debug_info(SEND_ERROR);
    return ERROR;
  }

  // Part 2
  switch (receive_message(&bytes_received, receive_buffer, sizeof(receive_buffer), responseTimeout))
  {
  case TYPE_ACK:
    show_debug_info(NO_ID_PART2_AVAILABLE);
    return SUCCESS;

  case TYPE_ID:
    show_debug_info(RECEIVED_ID_PART2);
    break;

  default:
    show_debug_info(UNEXPECTED_RESPONSE);
    return ERROR;
  }

  // Copy the string from the buffer to the struct, without overwriting the first part.
  memcpy(identification_data.identification + identification_string_index, receive_buffer, bytes_received);

  // Confirm receiving of the message and request the next.
  if (!acknowledge())
  {
    show_debug_info(SEND_ERROR);
    return ERROR;
  }

  // 3. Coding, WSC
  switch (receive_message(&bytes_received, receive_buffer, sizeof(receive_buffer), responseTimeout))
  {
  case TYPE_ACK:
    show_debug_info(NO_CODING_WSC_AVAILABLE);
    break;

  case TYPE_ID:
    show_debug_info(RECEIVED_CODING_WSC);
    break;

  default:
    show_debug_info(UNEXPECTED_RESPONSE);
    return ERROR;
  }

  // Extract the received coding value and workshop code.
  if (bytes_received == 5)
  {
    identification_data.coding = ((receive_buffer[1] << 8) | receive_buffer[2]) >> 1;
    identification_data.workshop_code = (uint32_t(receive_buffer[2] & 0x01) << 16) | uint16_t(receive_buffer[3] << 8) | receive_buffer[4];
  }
  // A control module may send a coding+WSC field with a data length of 1 instead of 5, which means coding is not supported, or may send an acknowledgement.
  else
  {
    identification_data.coding = identification_data.workshop_code = 0;
  }

  // If extra identification is available and it was requested, read it.
  if (extra_identification_available && request_extra_identification)
  {
    // 4. Extra identification
    show_debug_info(EXTRA_ID_AVAILABLE);

    // Request extra identification.
    if (!send_message(KWP_REQUEST_EXTRA_ID))
    {
      show_debug_info(SEND_ERROR);
      return ERROR;
    }

    // Part 1
    switch (receive_message(&bytes_received, receive_buffer, sizeof(receive_buffer), responseTimeout))
    {
    case TYPE_ACK:
      show_debug_info(NO_EXTRA_ID_PART1_AVAILABLE);
      return FAIL;

    case TYPE_ID:
      show_debug_info(RECEIVED_EXTRA_ID_PART1);
      break;

    default:
      show_debug_info(UNEXPECTED_RESPONSE);
      return ERROR;
    }

    // Copy the string from the buffer to the struct.
    memset(identification_data.extra_identification, 0, sizeof(identification_data.extra_identification));
    memcpy(identification_data.extra_identification, receive_buffer, bytes_received);
    size_t extra_identification_string_index = bytes_received;

    // Confirm receiving of the message and request the next.
    if (!acknowledge())
    {
      show_debug_info(SEND_ERROR);
      return ERROR;
    }

    // Part 2
    switch (receive_message(&bytes_received, receive_buffer, sizeof(receive_buffer), responseTimeout))
    {
    case TYPE_ACK:
      show_debug_info(NO_EXTRA_ID_PART2_AVAILABLE);
      return SUCCESS;

    case TYPE_ID:
      show_debug_info(RECEIVED_EXTRA_ID_PART2);
      break;

    default:
      show_debug_info(UNEXPECTED_RESPONSE);
      return ERROR;
    }

    // Copy the string from the buffer to the struct, without overwriting the first part.
    memcpy(identification_data.extra_identification + extra_identification_string_index, receive_buffer, bytes_received);
    extra_identification_string_index += bytes_received;

    // Confirm receiving of the message and request the next.
    if (!acknowledge())
    {
      show_debug_info(SEND_ERROR);
      return ERROR;
    }

    // Part 3
    switch (receive_message(&bytes_received, receive_buffer, sizeof(receive_buffer), responseTimeout))
    {
    case TYPE_ACK:
      show_debug_info(NO_EXTRA_ID_PART3_AVAILABLE);
      return SUCCESS;

    case TYPE_ID:
      show_debug_info(RECEIVED_EXTRA_ID_PART3);
      break;

    default:
      show_debug_info(UNEXPECTED_RESPONSE);
      return ERROR;
    }

    // Copy the string from the buffer to the struct, without overwriting the second part.
    memcpy(identification_data.extra_identification + extra_identification_string_index, receive_buffer, bytes_received);
  }
  else
  {
    show_debug_info(NO_EXTRA_ID_AVAILABLE);
  }

  // All identification data has been received.
  return SUCCESS;
}

/**
  Function:
    receive_keywords(uint8_t buffer[])

  Parameters:
    buffer -> array in which to store the received bytes

  Returns:
    bool -> whether or not the keywords were received successfully

  Description:
    Receives the 2 bytes sent by the module during initialization after the sync byte.
*/
bool KLineKWP1281Lib::receive_keywords(uint8_t *buffer)
{
  // Receive the two bytes.
  for (uint8_t i = 0; i < 2; i++)
  {
    // Attempt to receive a byte, without sending a complement, or exit if it times out.
    if (!read_byte(&buffer[i], initResponseTimeout, false))
    {
      K_LOG_ERROR("Error reading KW%d", i + 1);
      return false;
    }

    K_LOG_INFO("Received KW%d: %02X", i + 1, buffer[i]);
  }

  // Received both bytes.
  return true;
}

/**
  Function:
    read_byte(uint8_t *byte_out, unsigned long timeout_ms, bool must_send_complement)

  Parameters:
    byte_out             -> pointer to variable into which to store the received byte
    timeout_ms           -> how many milliseconds to wait for the byte
    must_send_complement -> whether or not to send a complement to the received byte

  Default parameters:
    must_send_complement = true

  Returns:
    bool -> whether or not a byte was received

  Description:
    Receives a single byte.

  Notes:
    *If receiving a byte times out, the error function is called.
*/
bool KLineKWP1281Lib::read_byte(uint8_t *byte_out, unsigned long timeout_ms, bool must_send_complement)
{
  // Try to receive a byte.
  if (!_receiveFunction(byte_out, pdMS_TO_TICKS(timeout_ms)))
  {
    K_LOG_ERROR("Reading byte timed out");

    // Call the error function only if currently connected to a module.
    if (_current_module)
    {
      error_function();
    }
    return false;
  }

  // Store the current time after receiving a byte, to delay properly when sending.
  last_byte_ticks = xTaskGetTickCount();

  // If a complement is necessary, send it after a small delay.
  if (must_send_complement)
  {
    vTaskDelay(pdMS_TO_TICKS(complementDelay));
    send_complement(*byte_out);
  }

  // Done reading the byte.
  return true;
}

/**
  Function:
    consume_UART_echo(uint8_t byte_in)

  Parameters:
    byte_in -> the byte expected to have been echoed

  Description:
    Consumes the echo generated on the K-line when sending a byte.
*/
void KLineKWP1281Lib::consume_UART_echo(uint8_t byte_in)
{
  uint8_t echo_byte;
  if (!_receiveFunction(&echo_byte, pdMS_TO_TICKS(echoTimeout)))
  {
    K_LOG_ERROR("Error reading echo; check interface");
  }
  else if (echo_byte != byte_in)
  {
    K_LOG_ERROR("Echo mismatch; check interface/baud rate");
  }
}

/**
  Function:
    send_complement(uint8_t byte_in)

  Parameters:
    byte_in -> byte to send the complement to

  Description:
    Sends a complement to a byte.
*/
void KLineKWP1281Lib::send_complement(uint8_t byte_in)
{
  // The complement is the difference between 0xFF and the received byte or the byte XORd by 0xFF.
  uint8_t complement = byte_in ^ 0xFF;
  _sendFunction(complement);

  // Because the K-line is bidirectional, an "echo" will appear.
  // Read that echoed byte to get rid of it from the serial buffer.
  // If the serial interface cannot receive while sending, this is not necessary.
  if (_full_duplex)
  {
    consume_UART_echo(complement);
  }
}

/**
  Function:
    receive_message(size_t *bytes_received_out, uint8_t buffer_out[], size_t buffer_size, unsigned long timeout_ms)

  Parameters:
    bytes_received_out -> pointer to variable into which to store the length of the received message
    buffer_out         -> array into which to store the received message
    buffer_size        -> total (maximum) length of the given array
    timeout_ms         -> milliseconds to wait for the message

  Returns:
    RETURN_TYPE -> what was received
      ERROR_TIMEOUT -> nothing (timed out)
      ERROR_OK      -> unknown
      TYPE_...      -> specific message

  Description:
    Receives a block (message).

  Notes:
    *Only the first byte will have a timeout of timeout_ms; every other byte will have a timeout of byteTimeout.
    *The error function is executed if receiving any byte times out.
*/
KLineKWP1281Lib::RETURN_TYPE KLineKWP1281Lib::receive_message(size_t *bytes_received_out, uint8_t *buffer_out, size_t buffer_size, unsigned long timeout_ms)
{
  // At the beginning of communication, after the modules sends protocol parameters, but before sending its identification strings, it might send those
  // parameters again.
  // In this case, the 0x55 sync byte should not receive an automatic complement (and should not be considered a block length).
  // Instead, two more bytes should be read (protocol description), and a manual complement sent to the second byte.
  // If a byte different than 0x55 is received, or if 0x55 is received but the module already sent its identification strings, it will be considered the block
  // length and will be complemented.
  while (true)
  {
    // Try to receive the length of the block.
    // Do not send an automatic complement in case the event described above takes place.
    // Exit if receiving times out.
    if (!read_byte(&receive_byte, timeout_ms, false))
    {
      K_LOG_ERROR("Response timed out");
      return ERROR_TIMEOUT;
    }

    // If the event described above takes place, proceed accordingly, after which wait for a byte again, to get the block length of the module's response.
    if (receive_byte == 0x55 && may_send_protocol_parameters_again)
    {
      K_LOG_INFO("Got SYNC byte again");

      // Try to receive the remaining two bytes of the protocol parameters.
      // If this times out, it could mean the 0x55 is waiting for a complement, so it's actually the block length (although improbable).
      if (!receive_keywords(keyword_buffer))
      {
        break;
      }

      // Send a complement to the second byte after a delay.
      vTaskDelay(pdMS_TO_TICKS(initComplementDelay));
      send_complement(keyword_buffer[1]);

      // Now, the loop will repeat, causing another byte read attempt.
      // If the received byte will not qualify for the special repeated protocol response, the loop will stop, the byte will be considered the block length and
      // complemented, and the block will continue to be read.
    }
    else
    {
      break;
    }
  }

  // The block length was received, complement it manually.
  delay(complementDelay);
  send_complement(receive_byte);

  // Determine how much data the message contains (length without the block title, the sequence, and the end byte).
  *bytes_received_out = receive_byte - 3;

  // Issue a debug warning if the data will not fit in the buffer.
  if (buffer_size < *bytes_received_out)
  {
    show_debug_info(ARRAY_NOT_LARGE_ENOUGH, *bytes_received_out);
  }

  // Try to receive the block's sequence number, or exit if receiving times out.
  if (!read_byte(&message_sequence, byteTimeout))
  {
    K_LOG_ERROR("Failed to receive message sequence");
    return ERROR_TIMEOUT;
  }

  // Try to receive the block's title (message type), or exit if receiving times out.
  uint8_t message_type;
  if (!read_byte(&message_type, byteTimeout))
  {
    K_LOG_ERROR("Failed to receive message type");
    return ERROR_TIMEOUT;
  }

  // If the block contains data bytes, attempt to read them.
  size_t buffer_index = 0;
  for (uint8_t i = 0; i < *bytes_received_out; i++)
  {
    // Try to receive a data byte, or exit if receiving times out.
    if (!read_byte(&receive_byte, byteTimeout))
    {
      K_LOG_ERROR("Failed to receive data byte #%d", buffer_index);
      return ERROR_TIMEOUT;
    }

    // If there is still space in the given buffer, save the received byte.
    if (buffer_size - buffer_index)
    {
      buffer_out[buffer_index++] = receive_byte;
    }
  }

  // Try to receive the block end byte, don't send a complement to it, or exit if receiving times out.
  if (!read_byte(&receive_byte, byteTimeout, false))
  {
    K_LOG_ERROR("Failed to receive message end byte");
    return ERROR_TIMEOUT;
  }
  else if (receive_byte != 0x03)
  {
    K_LOG_ERROR("Incorrect message end byte");
    return ERROR_TIMEOUT;
  }

  // Call the debugging function if it exists.
  if (_debugFunction)
  {
    _debugFunction(1, message_sequence, message_type, buffer_out, *bytes_received_out);
  }

  // Describe the received message type.
  show_debug_command_description(1, message_type);

  // Return based on the message type.
  switch (message_type)
  {
  case KWP_RECEIVE_ID_DATA:
    return TYPE_ID;

  case KWP_ACKNOWLEDGE:
    return TYPE_ACK;

  case KWP_REFUSE:
    return TYPE_REFUSE;

  case KWP_RECEIVE_FAULT_CODES:
    return TYPE_FAULT_CODES;

  case KWP_RECEIVE_ADAPTATION:
    return TYPE_ADAPTATION;

  case KWP_RECEIVE_GROUP_HEADER:
    return TYPE_GROUP_HEADER;

  case KWP_RECEIVE_GROUP_READING:
    return TYPE_GROUP_READING;

  case KWP_RECEIVE_ROM:
    return TYPE_ROM;

  case KWP_RECEIVE_OUTPUT_TEST:
    return TYPE_OUTPUT_TEST;

  case KWP_RECEIVE_BASIC_SETTING:
    return TYPE_BASIC_SETTING;

  default:
    return ERROR_OK;
  }
}

/**
  Function:
    acknowledge()

  Returns:
    bool -> whether or not the acknowledgement block was sent successfully

  Description:
    Tells the module that a block was received and that it can continue sending more data.
*/
bool KLineKWP1281Lib::acknowledge()
{
  // The acknowledgement message contains no data bytes.
  return send_message(KWP_ACKNOWLEDGE);
}

/**
  Function:
    send_byte(uint8_t byte_in, bool wait_for_complement)

  Parameters:
    byte_in             -> byte to send
    wait_for_complement -> whether or not to expect to receive a complement to the sent byte

  Default parameters:
    wait_for_complement = true

  Returns:
    bool -> whether or not the byte was sent successfully (if a complement was received when needed)

  Description:
    Sends a single byte.
*/
bool KLineKWP1281Lib::send_byte(uint8_t byte_in, bool wait_for_complement)
{
  // Ensure separation between bytes.
  vTaskDelayUntil(&last_byte_ticks, pdMS_TO_TICKS(byteDelay));

  // Send the byte.
  _sendFunction(byte_in);

  // If the serial interface can receive while sending, read the echoed byte to get rid of it.
  if (_full_duplex)
  {
    consume_UART_echo(byte_in);
  }
  
  // Wait for a complement, if needed.
  if (wait_for_complement)
  {
    uint8_t complement;
    if (!read_byte(&complement, complementResponseTimeout, false))
    {
      K_LOG_ERROR("Error reading complement");
      return false;
    }
    else if (complement != (byte_in ^ 0xFF))
    {
      K_LOG_ERROR("Incorrect complement received");
      return false;
    }
  }
  return true;
}

/**
  Function:
    send_message(uint8_t command, uint8_t parameters[], uint8_t parameter_count)

  Parameters:
    command                 -> opcode to send in the block
    parameters              -> array of parameters to provide in the block
    uint8_t parameter_count -> how many parameters the given array contains

  Returns:
    bool -> whether or not the block was sent successfully

  Default parameters:
    parameters[] = {}
    parameter_count = 0

  Description:
    Sends a block.
*/
bool KLineKWP1281Lib::send_message(uint8_t message_type, uint8_t *parameters, size_t parameter_count)
{
  // Ensure separation between blocks.
  // vTaskDelayUntil() is not used, as not to update last_byte_ticks.
  TickType_t elapsed_ticks = xTaskGetTickCount() - last_byte_ticks;
  if (elapsed_ticks < pdMS_TO_TICKS(blockDelay))
  {
    vTaskDelay(pdMS_TO_TICKS(blockDelay) - elapsed_ticks);
  }

  // The block size includes the sequence counter, the command byte, the parameter bytes and the final byte.
  size_t message_length = 3 + parameter_count;
  
  // Send the block size.
  if (!send_byte(message_length))
  {
    K_LOG_ERROR("Error sending message length");
    return false;
  }

  // Send the sequence number and also increment it.
  if (!send_byte(++message_sequence))
  {
    K_LOG_ERROR("Error sending message sequence");
    return false;
  }
  
  // Send the message type.
  if (!send_byte(message_type))
  {
    K_LOG_ERROR("Error sending message type");
    return false;
  }
  
  // If the message contains parameters, send them.
  if (parameters)
  {
    // Send each byte in the array.
    for (uint8_t i = 0; i < parameter_count; i++)
    {
      if (!send_byte(parameters[i]))
      {
        K_LOG_ERROR("Error sending data byte #%d", i);
        return false;
      }
    }
  }

  // Send the constant final byte and do not wait to receive a complement.
  send_byte(0x03, false);

  // Call the debugging function if it exists.
  if (_debugFunction)
  {
    _debugFunction(0, message_sequence, message_type, parameters, parameter_count);
  }

  // Describe the sent message type.
  show_debug_command_description(0, message_type);
  return true;
}

/**
  Function:
    error_function()

  Description:
    Reconnects to the module if a timeout occurs.
*/
void KLineKWP1281Lib::error_function()
{
  K_LOG_ERROR("The connection was terminated");

  // If the error function is disabled, don't reconnect.
  if (!error_function_allowed)
  {
    // Indicate that there is no module connected anymore.
    _current_module = 0;
    return;
  }
  
  // The module is already disconnected, ensure the disconnect() function is not executed again.
  uint8_t reconnect_to_module = _current_module;
  _current_module = 0;

  // If there is a custom function defined, execute it.
  if (_errorFunction)
  {
    // Give the custom error function the previous values.
    K_LOG_INFO("Calling the custom error function");
    _errorFunction(reconnect_to_module, _current_baud_rate);
  }
  // Otherwise, automatically reconnect to the previously connected module.
  else
  {
    // Reconnect using the previous values.
    K_LOG_INFO("Automatically reconnecting to module %02X with baud rate %lu", reconnect_to_module, _current_baud_rate);
    connect(reconnect_to_module, _current_baud_rate, false);
  }
}

/**
  Function:
    show_debug_info(DEBUG_TYPE type)

  Parameters:
    type -> event to print information about

  Description:
    Prints debugging information.
*/
void KLineKWP1281Lib::show_debug_info(DEBUG_TYPE type, uint8_t parameter)
{
  switch (type)
  {
  case UNEXPECTED_RESPONSE:
    K_LOG_ERROR("Unexpected response");
    break;

  case SEND_ERROR:
    K_LOG_ERROR("Sending error");
    break;

  case ARRAY_NOT_LARGE_ENOUGH:
    K_LOG_WARNING("The given buffer is too small for the incoming message (%d)", parameter);
    break;

  case LOGIN_ACCEPTED:
    K_LOG_DEBUG("Login accepted");
    break;

  case LOGIN_NOT_ACCEPTED:
    K_LOG_DEBUG("Login not accepted");
    break;

  case FAULT_CODES_NOT_SUPPORTED:
    K_LOG_DEBUG("Fault codes not supported");
    break;

  case CLEARING_FAULT_CODES_NOT_SUPPORTED:
    K_LOG_DEBUG("Clearing fault codes not supported");
    break;

  case CLEARING_FAULT_CODES_ACCEPTED:
    K_LOG_DEBUG("Cleared fault codes");
    break;

  case INVALID_ADAPTATION_CHANNEL:
    K_LOG_ERROR("Invalid adaptation channel");
    break;

  case INVALID_ADAPTATION_CHANNEL_OR_VALUE:
    K_LOG_ERROR("Invalid adaptation channel or value");
    break;

  case ADAPTATION_RECEIVED:
    K_LOG_DEBUG("Received adaptation channel");
    break;

  case ADAPTATION_ACCEPTED:
    K_LOG_DEBUG("Adaptation accepted");
    break;

  case ADAPTATION_NOT_ACCEPTED:
    K_LOG_DEBUG("Adaptation not accepted");
    break;

  case INVALID_BASIC_SETTING_GROUP:
    K_LOG_ERROR("Basic settings not supported, or invalid group requested");
    break;

  case RECEIVED_EMPTY_BASIC_SETTING_GROUP:
    K_LOG_ERROR("Empty basic setting group");
    break;

  case RECEIVED_BASIC_SETTING:
    K_LOG_DEBUG("Received basic setting");
    break;

  case INVALID_MEASUREMENT_GROUP:
    K_LOG_ERROR("Invalid measurement group");
    break;

  case RECEIVED_EMPTY_GROUP:
    K_LOG_ERROR("Empty measurement group");
    break;

  case RECEIVED_GROUP_HEADER:
    K_LOG_DEBUG("Received measurement group header");
    break;

  case INVALID_GROUP_HEADER_MAP_LENGTH:
    K_LOG_ERROR("Unexpected header table length (%d)", parameter);
    break;

  case RECEIVED_GROUP_BODY_OR_BASIC_SETTING:
    K_LOG_DEBUG("Received measurement group body or basic setting");
    break;

  case INVALID_GROUP_BODY_OR_BASIC_SETTING_LENGTH:
    K_LOG_ERROR("Expected 10/<4 bytes, got %d", parameter);
    break;

  case RECEIVED_GROUP:
    K_LOG_DEBUG("Received measurement group");
    break;

  case READ_ROM_NOT_SUPPORTED:
    K_LOG_ERROR("Reading ROM not supported, or invalid parameters given");
    break;

  case RECEIVED_ROM:
    K_LOG_DEBUG("Received ROM reading");
    break;

  case NO_PART_NUMBER_AVAILABLE:
    K_LOG_DEBUG("No part number available");
    break;

  case RECEIVED_PART_NUMBER:
    K_LOG_DEBUG("Received part number");
    break;

  case NO_ID_PART1_AVAILABLE:
    K_LOG_DEBUG("No identification available (part 1)");
    break;

  case RECEIVED_ID_PART1:
    K_LOG_DEBUG("Received identification (part 1)");
    break;

  case NO_ID_PART2_AVAILABLE:
    K_LOG_DEBUG("No identification available (part 2)");
    break;

  case RECEIVED_ID_PART2:
    K_LOG_DEBUG("Received identification (part 2)");
    break;

  case NO_CODING_WSC_AVAILABLE:
    K_LOG_DEBUG("No coding/WSC available");
    break;

  case RECEIVED_CODING_WSC:
    K_LOG_DEBUG("Received coding/WSC");
    break;

  case EXTRA_ID_AVAILABLE:
    K_LOG_DEBUG("Extra identification available");
    break;

  case NO_EXTRA_ID_AVAILABLE:
    K_LOG_DEBUG("No extra identification available or requested");
    break;

  case NO_EXTRA_ID_PART1_AVAILABLE:
    K_LOG_DEBUG("No extra identification available (part 1)");
    break;

  case RECEIVED_EXTRA_ID_PART1:
    K_LOG_DEBUG("Received extra identification (part 1)");
    break;

  case NO_EXTRA_ID_PART2_AVAILABLE:
    K_LOG_DEBUG("No extra identification available (part 2)");
    break;

  case RECEIVED_EXTRA_ID_PART2:
    K_LOG_DEBUG("Received extra identification (part 2)");
    break;

  case NO_EXTRA_ID_PART3_AVAILABLE:
    K_LOG_DEBUG("No extra identification available (part 3)");
    break;

  case RECEIVED_EXTRA_ID_PART3:
    K_LOG_DEBUG("Received extra identification (part 3)");
    break;

  case OUTPUT_TESTS_NOT_SUPPORTED:
    K_LOG_DEBUG("Output tests not supported");
    break;

  case RECEIVED_OUTPUT_TEST:
    K_LOG_DEBUG("Performing output test");
    break;

  case END_OF_OUTPUT_TESTS:
    K_LOG_DEBUG("End of output tests");
    break;

  case DISCONNECT_INFO:
    K_LOG_DEBUG("A \"timed out\" error is normal here");
    break;
  }
  
  (void)parameter;
}

/**
  Function:
    show_debug_command_description(uint8_t command)

  Parameters:
    command -> command which to describe

  Description:
    Describes the received command if debugging is enabled.
*/
void KLineKWP1281Lib::show_debug_command_description(bool direction, uint8_t command)
{
  switch (command)
  {
  /// SPECIAL
  case KWP_ACKNOWLEDGE:
    K_LOG_DEBUG("%s \"Acknowledgement\"", direction ? "Received" : "Sent");
    break;

  case KWP_REFUSE:
    K_LOG_DEBUG("%s \"Refuse\"", direction ? "Received" : "Sent");
    break;

  case KWP_DISCONNECT:
    K_LOG_DEBUG("%s \"Disconnect\"", direction ? "Received" : "Sent");
    break;

  /// SENT
  case KWP_REQUEST_EXTRA_ID:
    K_LOG_DEBUG("%s \"Request extra identification\"", direction ? "Received" : "Sent");
    break;

  case KWP_REQUEST_LOGIN:
    K_LOG_DEBUG("%s \"Login\"", direction ? "Received" : "Sent");
    break;

  case KWP_REQUEST_RECODE:
    K_LOG_DEBUG("%s \"Recode\"", direction ? "Received" : "Sent");
    break;

  case KWP_REQUEST_FAULT_CODES:
    K_LOG_DEBUG("%s \"Request fault codes\"", direction ? "Received" : "Sent");
    break;

  case KWP_REQUEST_CLEAR_FAULTS:
    K_LOG_DEBUG("%s \"Clear fault codes\"", direction ? "Received" : "Sent");
    break;

  case KWP_REQUEST_ADAPTATION:
    K_LOG_DEBUG("%s \"Request adaptation value\"", direction ? "Received" : "Sent");
    break;

  case KWP_REQUEST_ADAPTATION_TEST:
    K_LOG_DEBUG("%s \"Test adaptation value\"", direction ? "Received" : "Sent");
    break;

  case KWP_REQUEST_ADAPTATION_SAVE:
    K_LOG_DEBUG("%s \"Save adaptation value\"", direction ? "Received" : "Sent");
    break;

  case KWP_REQUEST_BASIC_SETTING:
  case KWP_REQUEST_BASIC_SETTING_0:
    K_LOG_DEBUG("%s \"Request basic setting\"", direction ? "Received" : "Sent");
    break;

  case KWP_REQUEST_GROUP_READING:
  case KWP_REQUEST_GROUP_READING_0:
    K_LOG_DEBUG("%s \"Request group reading\"", direction ? "Received" : "Sent");
    break;

  case KWP_REQUEST_READ_ROM:
    K_LOG_DEBUG("%s \"Request ROM reading\"", direction ? "Received" : "Sent");
    break;

  case KWP_REQUEST_OUTPUT_TEST:
    K_LOG_DEBUG("%s \"Request output test\"", direction ? "Received" : "Sent");
    break;

  /// RECEIVED
  case KWP_RECEIVE_ID_DATA:
    K_LOG_DEBUG("%s \"Provide identification\"", direction ? "Received" : "Sent");
    break;

  case KWP_RECEIVE_FAULT_CODES:
    K_LOG_DEBUG("%s \"Provide fault codes\"", direction ? "Received" : "Sent");
    break;

  case KWP_RECEIVE_ADAPTATION:
    K_LOG_DEBUG("%s \"Provide adaptation value\"", direction ? "Received" : "Sent");
    break;

  case KWP_RECEIVE_BASIC_SETTING:
    K_LOG_DEBUG("%s \"Provide basic setting/group body\"", direction ? "Received" : "Sent");
    break;

  case KWP_RECEIVE_GROUP_HEADER:
    K_LOG_DEBUG("%s \"Provide group header\"", direction ? "Received" : "Sent");
    break;

  case KWP_RECEIVE_GROUP_READING:
    K_LOG_DEBUG("%s \"Provide group reading\"", direction ? "Received" : "Sent");
    break;

  case KWP_RECEIVE_ROM:
    K_LOG_DEBUG("%s \"Provide ROM reading\"", direction ? "Received" : "Sent");
    break;

  case KWP_RECEIVE_OUTPUT_TEST:
    K_LOG_DEBUG("%s \"Execute output test\"", direction ? "Received" : "Sent");
    break;

  default:
    K_LOG_DEBUG("%s \"Unknown\"", direction ? "Received" : "Sent");
    break;
  }
  
  (void)direction;
}

uint8_t KLineKWP1281Lib::get_measurement_length(uint8_t *buffer, uint8_t bytes_received, uint8_t buffer_index)
{
  // Determine the current measurement's formula.
  uint8_t formula = buffer[buffer_index];
      
  // For formula 5F (ASCII text with known length) and 76 (HEX bytes with known length), the length is specified in the 2nd byte.
  if (formula == 0x5F || formula == 0x76)
  {
    // Also include the formula byte and the length byte themselves in the count.
    return buffer[buffer_index + 1] + 2;
  }
  // For formula 3F (ASCII text with unknown length), the entire (rest of the) response is used.
  else if (formula == 0x3F)
  {
    // It should only be possible to encounter 3F in readGroup(), after receiving it from the module
    // There, it is converted to 5F.
    // In all other places where the measurement length is needed, bytes_received will be passed as 0 because formula 3F is impossible.
    if (bytes_received)
    {
      return bytes_received - buffer_index;
    }
    else
    {
      return 0;
    }
  }
  // For formula A0 (measurement with variable units), the length is 6.
  else if (formula == 0xA0)
  {
    return 6;
  }
  
  // Normally, a measurement takes 3 bytes.
  return 3;
}

double KLineKWP1281Lib::ToSigned(double MW)
{
  return MW <= 127 ? MW : MW - 256;
}

double KLineKWP1281Lib::ToSigned(double NW, double MW)
{
  double value = NW * 256 + MW;
  return value <= 32767 ? value : value - 65536;
}

double KLineKWP1281Lib::To16Bit(double NW, double MW)
{
  return NW * 256 + MW;
}

int KLineKWP1281Lib::compare_keyed_structs(const void *a, const void *b)
{
  // Return the difference between the codes of the two structs.
  int idA = ((struct keyed_struct *)a)->code;
  int idB = ((struct keyed_struct *)b)->code;
  return (idA - idB);
}

const double KLineKWP1281Lib::negative_pow_10[] = {1, 0.1, 0.01, 0.001, 0.0001, 0.00001, 0.000001, 0.0000001};
