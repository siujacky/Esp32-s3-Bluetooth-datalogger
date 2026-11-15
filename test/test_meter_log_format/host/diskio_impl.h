#pragma once
#include "sdmmc_cmd.h"
#include "ff.h"
constexpr BYTE FF_DRV_NOT_USED=0xff;
inline int ff_diskio_get_drive(BYTE *drive){*drive=fakeCard.allocatedDrive;return *drive==0xff?-1:0;}
inline void ff_diskio_unregister(BYTE drive){if(drive==fakeCard.registeredDrive)fakeCard.registeredDrive=0xff;}
