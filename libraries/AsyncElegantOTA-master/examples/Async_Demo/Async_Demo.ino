/*
  (Deprecated) AsyncElegantOTA Demo Example
  -----

  This library has been deprecated. Please migrate to ElegantOTA v3
  New Repo: https://github.com/ayushsharma82/ElegantOTA
  Async Mode: https://docs.elegantota.pro/async-mode/
*/

#include <WiFi.h>
#include <AsyncTCP.h>

#include <ESPAsyncWebServer.h>
//#include <AsyncOTA.h>
#include <ESPUI.h>    // included for WiFi pages
#include <ESPmDNS.h>  // included for WiFi pages

#define ESPASYNCHTTPUPDATESERVER_MODE 1

#include <ESPAsyncHTTPUpdateServer.h>

const char* ssid = "........";
const char* password = "........";
//AsyncWebServer server(80);
ESPAsyncHTTPUpdateServer updateServer;

void setup(void) {
  Serial.begin(115200);
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(IPAddress(192, 168, 1, 1), IPAddress(192, 168, 1, 1), IPAddress(255, 255, 255, 0));
  WiFi.softAP("wifiHostName");

  //server.begin();
  Serial.println("HTTP server started");
  ESPUI.begin("wifiHostName");
  //AsyncElegantOTA.begin(ESPUI.WebServer());  // Start AsyncElegantOTA
  //AsyncOTA.begin(ESPUI.WebServer());

  updateServer.setup(ESPUI.WebServer(), "admin", "admin");

  updateServer.onUpdateBegin = [](const UpdateType type, int& result) {
    //you can force abort the update like this if you need to:
    //result = UpdateResult::UPDATE_ABORT;
    Serial.println("Update started : " + String(type));
  };
  updateServer.onUpdateEnd = [](const UpdateType type, int& result) {
    Serial.println("Update finished : " + String(type) + " result: " + String(result));
  };
}

void loop(void) {
}
