#pragma once
#include <stdint.h>
namespace meter_ui {
inline uint32_t recordCrc(const uint8_t *data,unsigned size){uint32_t crc=~uint32_t(0);for(unsigned i=0;i<size;++i){crc^=data[i];for(unsigned b=0;b<8;++b)crc=(crc>>1)^(0xedb88320u&uint32_t(-int(crc&1)));}return ~crc;}
}
