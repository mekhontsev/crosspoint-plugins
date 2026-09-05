#pragma once
#include <cstdint>
inline unsigned long millis() { return 1234; }
struct TestEsp {
  uint32_t getFreeHeap() { return 100000; }
  uint32_t getFreePsram() { return 7000000; }
};
inline TestEsp ESP;
