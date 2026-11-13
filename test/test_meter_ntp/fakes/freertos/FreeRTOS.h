#pragma once
#include <mutex>
#include <cstddef>
using portMUX_TYPE=std::mutex;
#define portMUX_INITIALIZER_UNLOCKED {}
#define portENTER_CRITICAL(mutexPointer) (mutexPointer)->lock()
#define portEXIT_CRITICAL(mutexPointer) (mutexPointer)->unlock()
#define pdTRUE 1
#define pdPASS 1
#define pdMS_TO_TICKS(ms) (ms)
