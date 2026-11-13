#pragma once
#include <string>
#include <cstring>
#include <cstdio>
using String=std::string;
struct FakeSerial {template<class... T> void printf(const char*,T...) {}};
extern FakeSerial Serial;
namespace fake {extern bool failWrite;extern unsigned writes;extern void (*unlock)();}
