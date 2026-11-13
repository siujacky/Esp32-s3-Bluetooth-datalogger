#pragma once
#include <stdint.h>
#include <string>
#include <utility>

namespace meter_rules {
enum class Operator { Above, Below };
struct Config {
    bool enabled = true;
    uint8_t meter = 0, channel = 0;
    std::string unit;
    Operator op = Operator::Above;
    double threshold = 0, hysteresis = 0;
    uint64_t cooldownMs = 1000;
};
struct Reading {
    uint8_t meter = 0, channel = 0;
    std::string unit;
    double value = 0;
    bool numeric = false;
    uint64_t uptimeMs = 0;
};
struct State {
    bool armed = true, hasFired = false;
    uint64_t lastFireMs = 0;
};
bool evaluate(const Config &config, State &state, const Reading &reading);
bool validUrl(const std::string &url);
bool validHeader(const std::string &name, const std::string &value);
std::string urlEncode(const std::string &value);
std::string render(const std::string &body, const std::string &value,
                   const std::string &unit, const std::string &meter,
                   const std::string &timestamp);
struct FeedCursor { uint64_t oldest=0,latest=0,next=0; bool gap=false; };
FeedCursor feedCursor(uint64_t latest,unsigned capacity,uint64_t after);
// The caller serializes access to live state throughout the save operation.
// This keeps evaluation/cooldown state from changing between staging and commit.
template<typename T,typename Save> bool commitIfSaved(T &live,T &staged,Save save) {
    if(!save(staged))return false;
    using std::swap;swap(live,staged);return true;
}
}
