#pragma once
#include <cstdint>
#include <string>

// Host-testable CSV and log-name boundary. This module has no filesystem API.
namespace meter_log_format {
struct Row {
    uint64_t seq=0,timestampMs=0,uptimeMs=0,droppedBefore=0;
    uint32_t packet=0,modeKey=0,flags=0,ruleId=0;
    unsigned meter=0,channel=0;
    int code=0;
    double value=0;
    bool numeric=false,reading=false,success=false;
    std::string deviceId,bootId,sessionId,label,event,message;
    std::string address,profile,text,unit,mode;
};
bool validLogName(const std::string &name);
bool validOperationId(const std::string &id);
std::string fileName(const std::string &sessionId);
std::string csvCell(const std::string &text);
std::string csvHeader();
std::string csvRow(const Row &row);
}
