#pragma once
#include <cstddef>
class Preferences {public:
    bool begin(const char *,bool);
    bool isKey(const char *);
    size_t getBytesLength(const char *);
    size_t getBytes(const char *,void *,size_t);
    size_t putBytes(const char *,const void *,size_t);
};
