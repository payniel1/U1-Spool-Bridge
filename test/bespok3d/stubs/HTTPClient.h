#pragma once
#include "Arduino.h"
#include "WiFi.h"
class HTTPClient {
 public:
  bool begin(WiFiClient &, const String &) { return true; }
  void setTimeout(int) {}
  void addHeader(const String &, const String &) {}
  int  POST(const String &) { return 200; }
  int  GET() { return 200; }
  String getString() { return String(""); }
  void end() {}
  static String errorToString(int) { return String(""); }
};
