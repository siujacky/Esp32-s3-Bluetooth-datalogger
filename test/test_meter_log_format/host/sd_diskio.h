#pragma once
#include "fake_runtime.h"
#include "ff.h"
inline uint8_t sdcard_init(uint8_t cs,SPIClass *spi,int frequency) {
    ++fakeCard.lowLevelInitCalls;
    if(cs!=10||spi->bus!=HSPI||frequency!=4000000)return 0xff;
    fakeCard.registeredDrive=fakeCard.allocatedDrive;return fakeCard.registeredDrive;
}
inline uint8_t sdcard_unmount(uint8_t drive){
    if(drive!=fakeCard.registeredDrive)return 1;
    const char logical[3]={static_cast<char>('0'+drive),':',0};
    return f_mount(nullptr,logical,0)==FR_OK?0:1;
}
inline uint8_t sdcard_uninit(uint8_t drive){if(drive!=fakeCard.registeredDrive)return 1;fakeCard.registeredDrive=0xff;return 0;}
