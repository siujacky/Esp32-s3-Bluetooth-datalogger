#pragma once
#include <Arduino.h>
#include "meter_ntp_state.h"

namespace meter_ntp {
using SyncCallback=void (*)(uint64_t unixMs);
struct Snapshot {
    bool ready=false,enabled=true,saved=false,pending=false;
    bool active=false,syncing=false,synced=false,waitingForNetwork=true;
    char server[serverBytes]="pool.ntp.org";
    char lastSyncServer[serverBytes]={};
    uint64_t lastSyncUnixMs=0,lastSyncUptimeMs=0,currentUnixMs=0;
    char error[192]={};
};
// Call once after WiFi has been initialized. NVS loading is asynchronous.
// False reports allocation/task-start failure through snapshot().error.
bool begin(SyncCallback callback);
// No flash/network work or waiting. Callback is invoked only here, with UTC
// milliseconds advanced by the elapsed monotonic time since the NTP response.
// Keep the application callback bounded; publish its anchor, then return.
void tick(bool stationConnected);
// Validate and enqueue. True means accepted, not persisted or synchronized.
// DNS hostname or dotted-decimal IPv4 only; no URLs, ports or IPv6 literals.
bool configure(bool enabled,const char *server,String &error);
// Queue an immediate retry. Disabled settings report an error asynchronously.
bool requestSync();
// Fixed-size copy, with currentUnixMs derived only from an accepted NTP event.
// synced/lastSync* describe this boot and remain useful after disable/link loss.
Snapshot snapshot();
}
