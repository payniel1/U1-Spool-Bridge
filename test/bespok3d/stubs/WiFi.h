#pragma once
#include "Arduino.h"
enum { WL_CONNECTED = 3 };
struct WiFiClass { int status() { return WL_CONNECTED; } };
extern WiFiClass WiFi;
struct WiFiClient {};
