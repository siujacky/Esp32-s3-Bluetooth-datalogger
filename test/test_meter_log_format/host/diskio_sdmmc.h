#pragma once
#include "diskio_impl.h"
inline void ff_diskio_register_sdmmc(BYTE drive,sdmmc_card_t *){++fakeCard.lowLevelInitCalls;fakeCard.registeredDrive=drive;}
inline BYTE ff_diskio_get_pdrv_card(const sdmmc_card_t *){return fakeCard.registeredDrive;}
