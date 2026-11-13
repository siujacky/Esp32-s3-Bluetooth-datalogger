#pragma once
#include "fake_runtime.h"
using BYTE=uint8_t;
using DWORD=uint32_t;
using UINT=unsigned;
struct FATFS {};
struct PARTITION {BYTE pd,pt;};
constexpr unsigned FF_VOLUMES=2,FF_MAX_SS=4096;
#define FF_MULTI_PARTITION 1
constexpr BYTE FM_FAT=1,FM_FAT32=2;
enum FRESULT {FR_OK=0,FR_DISK_ERR=1,FR_NOT_READY=3,FR_INVALID_DRIVE=11,FR_MKFS_ABORTED=14};
extern PARTITION VolToPart[FF_VOLUMES];
inline FRESULT f_mount(FATFS *,const char *drive,BYTE) {
    const unsigned number=static_cast<unsigned>(drive[0]-'0');
    if(number<FF_VOLUMES&&VolToPart[number].pd==0)fakeCard.internalVolumeMounted.store(false);
    std::lock_guard<std::mutex> guard(fakeCard.mutex);fakeCard.lastDetachedDrive=drive;return FR_OK;
}
inline FRESULT f_mkfs(const char *drive,BYTE flags,DWORD,void *,UINT bytes) {
    ++fakeCard.formatCalls;
    {std::lock_guard<std::mutex> guard(fakeCard.mutex);fakeCard.lastFormatDrive=drive;fakeCard.lastFormatFlags=flags;}
    if(fakeCard.pauseMkfs.load()) {
        fakeCard.mkfsBlocked.store(true);
        while(fakeCard.pauseMkfs.load())std::this_thread::sleep_for(std::chrono::milliseconds(1));
        fakeCard.mkfsBlocked.store(false);
    }
    if(!fakeCard.present.load())return FR_NOT_READY;
    if(fakeCard.failMkfs.exchange(false))return FR_MKFS_ABORTED;
    const unsigned number=static_cast<unsigned>(drive[0]-'0');
    if(bytes<FF_MAX_SS||drive[1]!=':'||drive[2]!=0||number!=fakeCard.registeredDrive||number>=FF_VOLUMES||VolToPart[number].pd!=number)return FR_INVALID_DRIVE;
    std::lock_guard<std::mutex> guard(fakeCard.mutex);
    fakeCard.nodes.clear();fakeCard.unformatted.store(false);fakeCard.formatCompleted.store(true);return FR_OK;
}
