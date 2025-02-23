/**
 * @file AsyncOTA.cpp
 * @brief Implementation of the AsyncOTA class for handling Over-The-Air (OTA) updates on ESP32.
 * @license MIT License
 *
 * This file contains the implementation of the AsyncOTA class, which provides methods to handle
 * OTA updates using an asynchronous web server.
 */

#include <AsyncOTA.h>

AsyncOTAClass AsyncOTA;

/**
 * Constructor for the AsyncOTA class
 */
AsyncOTAClass::AsyncOTAClass()
{
  // Generate a unique ID for the ESP32 chip using the MAC address
  String id = String((uint32_t)ESP.getEfuseMac(), HEX);
  id.toUpperCase();
  _id = id;
}

/**
 * Set the ESP32 chip ID or a custom ID for the device
 */
void AsyncOTAClass::setID(const char *id)
{
  _id = id;
}

/**
 * Get the ESP32 chip ID or the custom ID set by the user
 */
const String &AsyncOTAClass::getID()
{
  return _id;
}

/**
 * Set the username and password for authentication
 * @param username The username for authentication
 * @param password The password for authentication
 */
void AsyncOTAClass::setAuth(const char *username, const char *password)
{
  _username                 = username;
  _password                 = password;
  _isAuthenticationRequired = _username.length() && _password.length();
}

/**
 * Initialize the OTA update handler
 * Usage: Call this function in the setup function of the sketch
 * @param server The AsyncWebServer instance
 * @param username The username for authentication (optional)
 * @param password The password for authentication (optional)
 */
void AsyncOTAClass::begin(AsyncWebServer *server, const char *username, const char *password)
{
  _server = server;

  setAuth(username, password);

  /**
   * Return the current ESP32 chip ID
   */
  _server->on("/update/id", HTTP_GET, [&](AsyncWebServerRequest *request) {
    if (_isAuthenticationRequired && !request->authenticate(_username.c_str(), _password.c_str())) {
      return request->requestAuthentication();
    }

    request->send(200, "application/json", "{\"id\": \"" + getID() + "\"}");
  });

  /**
   * Return the HTML page for OTA update
   */
  _server->on("/update", HTTP_GET, [&](AsyncWebServerRequest *request) {
    if (_isAuthenticationRequired && !request->authenticate(_username.c_str(), _password.c_str())) {
      return request->requestAuthentication();
    }
    AsyncWebServerResponse *response = request->beginResponse(200, "text/html", OTA_HTML, sizeof(OTA_HTML));
    response->addHeader("Content-Encoding", "gzip");
    request->send(response);
  });

  /**
   * Handle CORS preflight request
   */
  _server->on("/update", HTTP_OPTIONS, [&](AsyncWebServerRequest *request) {
    if (_isAuthenticationRequired && !request->authenticate(_username.c_str(), _password.c_str())) {
      return request->requestAuthentication();
    }

    request->send(200);
  });

  /**
   * Handle the OTA update POST request
   */
  _server->on(
      "/update", HTTP_POST,
      [&](AsyncWebServerRequest *request) {
        // the request handler is triggered after the upload has finished...
        // create the response, add header, and send response
        bool hasError = Update.hasError();

        AsyncWebServerResponse *response =
            request->beginResponse(hasError ? 500 : 200, "text/plain", hasError ? _updateError.c_str() : "OK");
        response->addHeader("Connection", "close");
        request->send(response);

        // Delay to allow response to be sent to client before rebooting the device
        _rebootRequestMillis = millis();
        _shouldReboot        = true;
      },
      [&](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
        // if index == 0 then this is the first frame of data
        if (index == 0) {

          // If authentication is required and the request is not authenticated, request authentication
          // onUpload handler is called before the onRequest handler
          if (_isAuthenticationRequired && !request->authenticate(_username.c_str(), _password.c_str())) {
            return request->requestAuthentication();
          }

          auto md5 = request->getParam("md5", true);
          if (!md5 || !Update.setMD5(md5->value().c_str())) {
            return request->send(400, "text/plain", "MD5 parameter missing or invalid");
          }

          // increase the watchdog timer to prevent panic
          esp_task_wdt_init(15, 0);

          if (request->hasParam("mode", true) && request->getParam("mode", true)->value() == "fs") {
            _updateMode = U_SPIFFS;
          }

          // Start the OTA update
          if (!Update.begin(UPDATE_SIZE_UNKNOWN, _updateMode)) {
            trackError();
            return request->send(400, "text/plain", "OTA could not begin");
          }
        }

        // write data chunk to the OTA update
        if (len) {
          if (Update.write(data, len) != len) {
            trackError();
            return request->send(400, "text/plain", "OTA could not begin");
          }
        }

        if (final) {               // if the final flag is set then this is the last frame of data
          if (!Update.end(true)) { // true to set the size to the current progress
            trackError();
            return request->send(400, "text/plain", "Could not end OTA");
          }
        }
      });
}

/**
 * Loop function to reboot the device after the OTA update has been completed
 * This is done to ensure that the device is rebooted after the response has been sent to the client
 * Usage: Call this function in the main loop of the sketch
 */
void AsyncOTAClass::loop()
{
  if (_shouldReboot && millis() - _rebootRequestMillis > 2000) {
    ESP.restart();
  }
}

/**
 * Track the error that occurred during the OTA update in a string
 */
void AsyncOTAClass::trackError()
{
  StreamString str;
  Update.printError(str);
  _updateError = str.c_str();
}