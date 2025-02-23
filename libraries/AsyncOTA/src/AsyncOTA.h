/**
 * @file AsyncOTA.h
 * @brief Implementation of the AsyncOTA class for handling Over-The-Air (OTA) updates on ESP32.
 * @license MIT License
 *
 * This file contains the implementation of the AsyncOTA class, which provides methods to handle
 * OTA updates using an asynchronous web server.
 */

#ifndef ASYNCOTA_H
#define ASYNCOTA_H

#include "Arduino.h"
#include "stdlib_noniso.h"

#include "AsyncTCP.h"
#include "FS.h"
#include "Update.h"
#include "WiFi.h"
#include "esp_int_wdt.h"
#include "esp_task_wdt.h"

#include "ESPAsyncWebServer.h"

#include "OtaHTML.h"

class AsyncOTAClass
{
public:
  AsyncOTAClass();
  void setID(const char *id);
  const String &getID();
  void begin(AsyncWebServer *server, const char *username = "", const char *password = "");
  void setAuth(const char *username, const char *password);
  void loop();

private:
  void trackError();

  AsyncWebServer *_server;

  String _id;
  bool _isAuthenticationRequired = false;
  int _updateMode                = U_FLASH;
  String _username;
  String _password;

  bool _shouldReboot                 = false;
  unsigned long _rebootRequestMillis = 0;
  String _updateError;
};

extern AsyncOTAClass AsyncOTA;

#endif
