#pragma once
#include <map>
#include <string>
#include <stddef.h>
#include <stdint.h>
extern std::map<std::string,bool> fakeSaved;
extern std::map<std::string,uint8_t> fakeSavedBytes;
extern bool fakeOffsetWriteFails,fakeOffsetReadbackFails;
extern unsigned fakeOffsetWrites;
struct Preferences {
    bool begin(const char *,bool){return true;}
    bool getBool(const char *key,bool fallback){return fakeSaved.count(key)?fakeSaved[key]:fallback;}
    size_t putBool(const char *key,bool v){fakeSaved[key]=v;return 1;}
    uint8_t getUChar(const char *key,uint8_t fallback){return fakeOffsetReadbackFails?0xff:fakeSavedBytes.count(key)?fakeSavedBytes[key]:fallback;}
    size_t putUChar(const char *key,uint8_t v){++fakeOffsetWrites;if(fakeOffsetWriteFails)return 0;fakeSavedBytes[key]=v;return 1;}
};
