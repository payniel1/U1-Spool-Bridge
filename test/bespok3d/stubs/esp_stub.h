#pragma once
#include "Arduino.h"
struct EspClass {
  uint64_t getEfuseMac() { return 0x001122334455ULL; }
  void     restart() {}
  uint32_t getFreeHeap() { return 100000; }
};
extern EspClass ESP;
