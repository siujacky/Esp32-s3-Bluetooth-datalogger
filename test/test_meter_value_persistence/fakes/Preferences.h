#pragma once
#include <Arduino.h>
#include <vector>
#include <map>
class Preferences {public:
static std::map<std::string,std::vector<unsigned char>>& records(){static std::map<std::string,std::vector<unsigned char>> data;return data;}
bool begin(const char*,bool){return true;} bool isKey(const char *key){return records().count(key)>0;}
size_t getBytesLength(const char *key){auto it=records().find(key);return it==records().end()?0:it->second.size();}
size_t getBytes(const char *key,void *out,size_t n){auto it=records().find(key);if(it==records().end()||n!=it->second.size())return 0;memcpy(out,it->second.data(),n);return n;}
size_t putBytes(const char *key,const void *in,size_t n){++fake::writes;if(fake::failWrite)return 0;records()[key].assign((const unsigned char*)in,(const unsigned char*)in+n);return n;}
};
