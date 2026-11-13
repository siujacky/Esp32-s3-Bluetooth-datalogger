#ifdef METER_NTP_JSON_TEST
#include <cassert>
#include <cstring>
#include <string>
#include "../../src/esp32_touch/meter/meter_ntp.h"
#include "../../src/esp32_touch/meter/meter_ntp_json.h"
int main(){
 meter_ntp::Snapshot snapshot;
 const meter_ntp::Snapshot &input=snapshot;
 JsonDocument d;meter_ntp::writeSnapshotJson(d.to<JsonObject>(),input);
 assert(d["server"].as<JsonString>().size()==strlen("pool.ntp.org"));
 assert(d["lastSyncServer"].as<JsonString>().size()==0);
 assert(d["error"].as<JsonString>().size()==0);
 memset(snapshot.server,'x',sizeof(snapshot.server));
 assert(strcmp(d["server"].as<const char*>(),"pool.ntp.org")==0);
 std::string wire;serializeJson(d,wire);assert(wire.size()==measureJson(d));
 JsonDocument parsed;assert(!deserializeJson(parsed,wire));
 assert(parsed["server"]=="pool.ntp.org");
 puts("PASS actual NTP JSON serializer owns strings; snapshot changes cannot corrupt JSON/Content-Length");
}
#endif
