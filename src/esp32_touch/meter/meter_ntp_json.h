#pragma once
#include <ArduinoJson.h>
namespace meter_ntp {
// This helper owns all JSON string values beyond the snapshot lifetime.
template<class SnapshotType> void writeSnapshotJson(JsonObject out,const SnapshotType &n){out["ready"]=n.ready;out["enabled"]=n.enabled;out["server"]=JsonString(n.server);out["saved"]=n.saved;out["pending"]=n.pending;out["active"]=n.active;out["syncing"]=n.syncing;out["synced"]=n.synced;out["waitingForNetwork"]=n.waitingForNetwork;out["lastSyncServer"]=JsonString(n.lastSyncServer);out["lastSyncUnixMs"]=n.lastSyncUnixMs;out["lastSyncUptimeMs"]=n.lastSyncUptimeMs;out["currentUnixMs"]=n.currentUnixMs;out["error"]=JsonString(n.error);}
}
