#pragma once
#include <stdint.h>
#include <stddef.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <algorithm>
using portMUX_TYPE=int;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(p) ((void)(p))
#define portEXIT_CRITICAL(p) ((void)(p))
extern uint32_t fakeMillis;
inline uint32_t millis(){return fakeMillis;}
inline size_t strlcpy(char *to,const char *from,size_t capacity){const size_t n=strlen(from);if(capacity){const size_t k=n<capacity-1?n:capacity-1;memcpy(to,from,k);to[k]=0;}return n;}
struct FakeSerial {template<class... T>void printf(const char *,T...) {}};
extern FakeSerial Serial;
constexpr int OUTPUT=1,LOW=0,HIGH=1;
extern unsigned fakeDuty;
inline void pinMode(int,int){}inline void digitalWrite(int,int){}
inline void delay(unsigned ms){fakeMillis+=ms;}
inline void ledcSetup(int,int,int){}inline void ledcAttachPin(int,int){}
inline void ledcWrite(int,unsigned duty){fakeDuty=duty;}
inline void *ps_malloc(size_t n){return malloc(n);}
