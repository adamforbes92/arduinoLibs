# AsyncOTA

AsyncOTA is a library that allows you to update your ESP32 firmware over-the-air (OTA) using the AsyncTCP / ESPAsyncWebServer library.

## Features

- Supports firmware updates over-the-air (OTA)
- Uses AsyncTCP / ESPAsyncWebServer for ESP32

## Build web interface

Install frontend dependencies:

```bash
yarn && yarn build
```

## Implementation

### Add AsyncOTA to your platformIO project platformio.ini

```ini
[env:esp32]
platform = espressif32
board = esp32dev
framework = arduino
monitor_speed = 115200
lib_deps =
    circuitcode/AsyncOTA
```

### Include the library in your code

```cpp
#include <AsyncOTA.h>
```

### Initialize the library

```cpp
AsyncWebServer server(80);
```

### Use the library

```cpp
void setup() {
 AsyncOTA.begin(&server);
 server.begin();
}

void loop() {
 AsyncOTA.loop();
}
```

### Add CORS headers (optional)

Create a custom middleware function to add CORS headers to the response.

```
AsyncMiddlewareFunction cors([](AsyncWebServerRequest *request, ArMiddlewareNext next) {
  next();

  String origin = request->header("Origin");
  if (origin.length() > 0) {
    request->getResponse()->addHeader("Access-Control-Allow-Origin", origin);
  } else {
    request->getResponse()->addHeader("Access-Control-Allow-Origin", "*");
  }

  request->getResponse()->addHeader("Access-Control-Allow-Methods", "*");
  request->getResponse()->addHeader("Access-Control-Allow-Headers", "*");
  request->getResponse()->addHeader("Access-Control-Allow-Credentials", "true");
});
```

Then add the middleware before starting the server.

```cpp
server.addMiddleware(&cors);
```

The library will manage automatically the CORS preflight request.

### Use authentication (optional)

Initialize the library with a username and password.

```cpp
AsyncOTA.begin(&server, "username", "password");
```

### Set a custom hardware ID (optional)

Set a custom hardware ID for the firmware update page.

```cpp
AsyncOTA.setID("my-device");
```

### Connect to the device and upload the firmware

Navigate to `http://<device-ip>/update` to access the firmware update page.


## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.
