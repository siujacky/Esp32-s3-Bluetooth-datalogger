#include "meter_log_format.h"
#include <cmath>
#include <cstdio>
#include <cctype>
namespace meter_log_format {
namespace {
bool validSession(const std::string &id) {
    // The web recorder generates 16 random hexadecimal boot digits followed
    // by a nonzero uint32 session counter. Never accept user labels as paths.
    if(id.size()<18||id.size()>27||id[16]!='-')return false;
    for(size_t i=0;i<16;++i)if(!std::isxdigit(static_cast<unsigned char>(id[i])))return false;
    if(id[17]<'1'||id[17]>'9')return false;
    uint64_t value=0;
    for(size_t i=17;i<id.size();++i) {
        if(id[i]<'0'||id[i]>'9')return false;
        value=value*10+static_cast<unsigned>(id[i]-'0');
        if(value>UINT32_MAX)return false;
    }
    return true;
}
std::string number(uint64_t n) {char text[24];std::snprintf(text,sizeof(text),"%llu",static_cast<unsigned long long>(n));return text;}
std::string signedNumber(int n) {char text[16];std::snprintf(text,sizeof(text),"%d",n);return text;}
std::string valueText(double n) {char text[40];std::snprintf(text,sizeof(text),"%.17g",n);return text;}
}
bool validLogName(const std::string &name) {
    if(name.size()<28||name.compare(0,6,"meter-")!=0||name.compare(name.size()-4,4,".csv")!=0)return false;
    return validSession(name.substr(6,name.size()-10));
}
bool validOperationId(const std::string &id) {
    if(id.size()<16||id.size()>64)return false;
    for(unsigned char c:id)if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-'))return false;
    return true;
}
std::string fileName(const std::string &sessionId) {return validSession(sessionId)?"meter-"+sessionId+".csv":"";}
std::string csvCell(const std::string &text) {
    bool formula=!text.empty()&&(text[0]=='\t'||text[0]=='\r'||text[0]=='\n');
    size_t first=0;
    while(first<text.size()&&std::isspace(static_cast<unsigned char>(text[first])))++first;
    if(first<text.size()&&(text[first]=='='||text[first]=='+'||text[first]=='-'||text[first]=='@'))formula=true;
    std::string out="\"";
    if(formula)out+='\'';
    for(char c:text) {out+=c;if(c=='"')out+='"';}
    return out+'"';
}
std::string csvHeader() {
    return "deviceId,bootId,seq,kind,event,sessionId,label,meter,channel,timestampMs,uptimeMs,numeric,value,text,unit,mode,modeKey,flags,address,profile,packet,ruleId,success,code,message,droppedBefore\r\n";
}
std::string csvRow(const Row &r) {
    const bool numeric=r.reading&&r.numeric&&std::isfinite(r.value);
    std::string out;out.reserve(768);
    auto text=[&out](const std::string &value){if(!out.empty())out+=',';out+=csvCell(value);};
    auto raw=[&out](const std::string &value){out+=',';out+=value;};
    text(r.deviceId);text(r.bootId);raw(number(r.seq));text(r.reading?"reading":"event");
    text(r.event);text(r.sessionId);text(r.label);raw(number(r.meter));raw(number(r.channel));
    raw(r.timestampMs?number(r.timestampMs):"");raw(number(r.uptimeMs));text(numeric?"true":"false");
    raw(numeric?valueText(r.value):"");text(r.text);text(r.unit);text(r.mode);
    raw(number(r.modeKey));raw(number(r.flags));text(r.address);text(r.profile);
    raw(number(r.packet));raw(number(r.ruleId));text(r.success?"true":"false");
    raw(signedNumber(r.code));text(r.message);raw(number(r.droppedBefore));
    return out+"\r\n";
}
}
