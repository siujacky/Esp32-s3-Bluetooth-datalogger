#pragma once
#include <cstdint>
inline uint32_t millis(){return 0;}
struct FakeSerial {template<class... Args>void printf(const char *,Args...){} };
static FakeSerial Serial;
