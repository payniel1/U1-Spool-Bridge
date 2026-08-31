#pragma once
#include "Arduino.h"
class Preferences {
 public:
  bool begin(const char *, bool = false) { return true; }
  void end() {}
  size_t getBytesLength(const char *) { return 0; }
  size_t getBytes(const char *, void *, size_t) { return 0; }
  size_t putBytes(const char *, const void *, size_t) { return 0; }
  bool   remove(const char *) { return true; }
  bool   clear() { return true; }
  size_t putString(const char *, const char *) { return 0; }
  String getString(const char *, const char * = "") { return String(""); }
  bool   isKey(const char *) { return false; }
};
