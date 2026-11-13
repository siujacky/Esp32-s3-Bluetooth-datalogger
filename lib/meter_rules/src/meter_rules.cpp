#include "meter_rules.h"
#include <cmath>
namespace meter_rules {
bool evaluate(const Config &c, State &s, const Reading &r) {
    if (!c.enabled || !r.numeric || r.meter!=c.meter || r.channel!=c.channel ||
        r.unit!=c.unit || !std::isfinite(r.value) || !std::isfinite(c.threshold) ||
        !std::isfinite(c.hysteresis) || c.hysteresis<0) return false;
    const bool condition=c.op==Operator::Above ? r.value>c.threshold : r.value<c.threshold;
    const bool clear=c.op==Operator::Above ? r.value<=c.threshold-c.hysteresis : r.value>=c.threshold+c.hysteresis;
    if (clear) s.armed=true;
    if (!condition || !s.armed) return false;
    // A crossing during cooldown is consumed. A new departure and re-entry is
    // required, rather than delivering a delayed request while the value stays high.
    s.armed=false;
    if (s.hasFired && (r.uptimeMs<s.lastFireMs || r.uptimeMs-s.lastFireMs<c.cooldownMs)) return false;
    s.hasFired=true; s.lastFireMs=r.uptimeMs;
    return true;
}
static bool alphaNum(unsigned char c) {
    return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9');
}
bool validUrl(const std::string &url) {
    size_t start=url.compare(0,7,"http://")==0 ? 7 : url.compare(0,8,"https://")==0 ? 8 : 0;
    if (!start || url.size()>512) return false;
    for (unsigned char c:url) if (c<=32 || c==127 || c=='\\' || c=='#') return false;
    size_t end=url.find_first_of("/?",start);
    if (end==std::string::npos) end=url.size();
    std::string authority=url.substr(start,end-start);
    if (authority.empty() || authority.find('@')!=std::string::npos) return false;
    size_t portAt=std::string::npos;
    if (authority[0]=='[') {
        size_t close=authority.find(']');
        if (close==std::string::npos || close<3) return false;
        for (size_t i=1;i<close;i++) {
            const char c=authority[i];
            if (!((c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F')||c==':'||c=='.')) return false;
        }
        if (close+1<authority.size()) { if (authority[close+1]!=':') return false; portAt=close+1; }
    } else {
        portAt=authority.find(':');
        const size_t hostEnd=portAt==std::string::npos ? authority.size() : portAt;
        if (!hostEnd || authority[0]=='.' || authority[hostEnd-1]=='.') return false;
        for (size_t i=0;i<hostEnd;i++) if (!alphaNum(authority[i])&&authority[i]!='-'&&authority[i]!='.') return false;
    }
    if (portAt!=std::string::npos) {
        if (portAt+1==authority.size()) return false;
        unsigned port=0;
        for (size_t i=portAt+1;i<authority.size();i++) {
            if (authority[i]<'0'||authority[i]>'9') return false;
            port=port*10+unsigned(authority[i]-'0'); if (port>65535) return false;
        }
        if (!port) return false;
    }
    return true;
}
bool validHeader(const std::string &name,const std::string &value) {
    if (name.empty()||name.size()>48||value.size()>256) return false;
    std::string lower;
    for (unsigned char c:name) {
        if (!alphaNum(c)&&std::string("!#$%&'*+-.^_`|~").find(c)==std::string::npos) return false;
        lower+=c>='A'&&c<='Z' ? char(c+32) : char(c);
    }
    if (lower=="host"||lower=="content-length"||lower=="transfer-encoding"||lower=="connection"||lower=="upgrade"||lower=="expect"||lower=="trailer") return false;
    for (unsigned char c:value) if (c<32 || c==127) return false;
    return true;
}
std::string urlEncode(const std::string &value) {
    static const char digits[]="0123456789ABCDEF";
    std::string result;
    for (unsigned char c:value) {
        if (alphaNum(c)||c=='-'||c=='_'||c=='.'||c=='~') result+=c;
        else { result+='%';result+=digits[c>>4];result+=digits[c&15]; }
    }
    return result;
}
std::string render(const std::string &body,const std::string &value,const std::string &unit,const std::string &meter,const std::string &timestamp) {
    std::string out;
    for (size_t at=0;at<body.size();) {
        const char *tokens[]={"{{value}}","{{unit}}","{{meter}}","{{timestamp}}"};
        const std::string *values[]={&value,&unit,&meter,&timestamp};
        bool matched=false;
        for (unsigned n=0;n<4;n++) {
            const std::string token=tokens[n];
            if (body.compare(at,token.size(),token)==0) { out+=*values[n];at+=token.size();matched=true;break; }
        }
        if (!matched) out+=body[at++];
    }
    return out;
}
FeedCursor feedCursor(uint64_t latest,unsigned capacity,uint64_t after) {
    FeedCursor c;c.latest=latest;
    if(!latest||!capacity) {c.gap=after>0;return c;}
    c.oldest=latest>=capacity ? latest-capacity+1 : 1;
    c.gap=after<c.oldest-1||after>latest;
    c.next=c.gap ? c.oldest-1 : after;
    return c;
}
}
