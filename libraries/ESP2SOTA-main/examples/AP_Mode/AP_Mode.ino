#include <WiFi.h>
#include <AsyncTCP.h>
/* INCLUDE ESP2SOTA LIBRARY */
#include <ESPAsyncWebServer.h>
#include <ESPUI.h>    // included for WiFi pages
#include <ESPmDNS.h>  // included for WiFi pages
#include <ESP2SOTA.h>

const char* ssid = "ESP2SOTA";
const char* password = "123456789abc";

//WebServer server(80);

void setup(void) {
  Serial.begin(115200);
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid);
  delay(1000);
  IPAddress IP = IPAddress(192, 168, 1, 1);
  IPAddress NMask = IPAddress(255, 255, 255, 0);
  WiFi.softAPConfig(IP, IP, NMask);
  IPAddress myIP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(myIP);

  /* INITIALIZE ESP2SOTA LIBRARY */

  //server.begin();

  ESPUI.begin("wifiHostName");
  ESP2SOTA.begin(ESPUI.WebServer());
}

void loop(void) {
  /* HANDLE UPDATE REQUESTS */
 // server.handleClient();

  /* YOUR LOOP CODE HERE */

  Serial.println("cunt");

  delay(5);
}