
#pragma once

#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include "hardware/pin_config.h"
#include <Adafruit_PCF8574.h>

extern WiFiClientSecure client;
extern HTTPClient http;
extern Adafruit_PCF8574 pcf8574;
