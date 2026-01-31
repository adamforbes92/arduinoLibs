#include <Arduino.h>
#include <AsyncOTA.h>
#include <ESPAsyncWebServer.h>

AsyncWebServer server(80);

void setup()
{
    AsyncOTA.begin(&server);
    server.begin();
}

void loop()
{
    AsyncOTA.loop();
    delay(10);
}