#pragma once
#include <Arduino.h>
using SemaphoreHandle_t=int*;
inline SemaphoreHandle_t xSemaphoreCreateMutex(){static int v=1;return &v;}
inline void xSemaphoreTake(SemaphoreHandle_t,unsigned){}
inline void xSemaphoreGive(SemaphoreHandle_t){static bool active=false;if(fake::unlock&&!active){active=true;fake::unlock();active=false;}}
