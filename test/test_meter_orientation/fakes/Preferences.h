#pragma once
#include <map>
#include <string>
#include <cstddef>
#include <cstdint>
namespace fake_preferences {
inline std::map<std::string,uint8_t> &bytes(){static std::map<std::string,uint8_t> value;return value;}
inline bool &beginOK(){static bool value=true;return value;}
inline bool &writeOK(){static bool value=true;return value;}
inline bool &readbackOK(){static bool value=true;return value;}
inline std::string &openedNamespace(){static std::string value;return value;}
inline unsigned &writes(){static unsigned value=0;return value;}
}
class Preferences {
public:
    bool begin(const char *name,bool){fake_preferences::openedNamespace()=name;return fake_preferences::beginOK();}
    bool isKey(const char *key){return fake_preferences::bytes().count(key)!=0;}
    uint8_t getUChar(const char *key,uint8_t fallback=0){const auto item=fake_preferences::bytes().find(key);if(item==fake_preferences::bytes().end())return fallback;if(std::string(key)=="rotation"&&!fake_preferences::readbackOK())return 255;return item->second;}
    size_t putUChar(const char *key,uint8_t value){++fake_preferences::writes();if(!fake_preferences::writeOK())return 0;fake_preferences::bytes()[key]=value;return 1;}
};
