#pragma once
#include "meter_web.h"

namespace meter_storage {
struct Record {
    uint64_t seq=0,timestampMs=0;
    meter_web::Sample sample;
    char sessionId[40]={},label[97]={},bootId[24]={},deviceId[24]={};
    char event[20]={},message[112]={};
    uint32_t ruleId=0;
    int code=0;
    bool success=false,reading=false,stopSession=false;
};
// Starts an independent worker. Mounting is asynchronous and never formats.
// False means the worker could not start; consult statusJson() for the error.
bool begin();
// Fixed-size queue copy only: no allocation, waits, formatting or filesystem
// access. May be called while the feed lock preserves sequence order. Empty
// session IDs are ignored; false means a tagged record was not accepted.
bool append(const Record &record);
String statusJson();
// Cached, bounded directory snapshot, including active-file byte counts.
// error reports listing failures; storageError carries prior storage warnings.
String filesJson();
// Queue a mount retry without formatting. Refuses active/queued recordings,
// downloads, or another remount. A queued result is not a successful mount.
bool requestRemount(String &error);
// Explicit destructive operation, queued only when every card user is idle.
// Caller must obtain the user's format confirmation before calling. A bounded
// history deduplicates 16..64-byte ASCII alnum/_/- operation IDs.
bool requestFormat(const String &operationId,String &error);
// One download at a time. The returned nonzero token owns a read handle whose
// initial length stays fixed while recording appends. Each read is <=1024
// bytes and releases the filesystem mutex; callers must always close token.
uint32_t openDownload(const String &name,uint64_t &snapshotBytes,String &error);
size_t readDownload(uint32_t token,uint8_t *target,size_t maxBytes,String &error);
void closeDownload(uint32_t token);
}
