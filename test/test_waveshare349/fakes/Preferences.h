#pragma once
#include <map>
#include <string>
#include <stddef.h>
extern std::map<std::string,bool> fakeSaved;
struct Preferences {
    bool begin(const char *,bool){return true;}
    bool getBool(const char *key,bool fallback){return fakeSaved.count(key)?fakeSaved[key]:fallback;}
    size_t putBool(const char *key,bool v){fakeSaved[key]=v;return 1;}
};
