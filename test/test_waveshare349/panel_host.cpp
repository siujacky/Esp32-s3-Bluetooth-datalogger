#include "panel.h"
#include <Wire.h>
#include <driver/spi_master.h>
#include <cassert>
#include <cstdio>
uint32_t fakeMillis=0;unsigned fakeDuty=0;FakeSerial Serial;FakeWire Wire,Wire1;
spi_device_interface_config_t fakeSpiConfig;
std::vector<SpiRecord> fakeSpiRecords;
namespace meter_board {bool begin(){return true;}bool lcdReset(bool){return true;}bool backlightEnable(bool){return true;}}
int main(){
    DeskPanel panel;assert(panel.begin());assert(fakeDuty>0);
    assert(fakeSpiConfig.mode==3&&fakeSpiConfig.command_bits==8&&fakeSpiConfig.address_bits==24&&fakeSpiConfig.clock_speed_hz==40000000);
    fakeSpiRecords.clear();panel.getBuffer()[0]=0xf800;panel.flush();
    assert(fakeSpiRecords.size()==21);assert(fakeSpiRecords[0].cmd==2&&fakeSpiRecords[0].reg==0x2a);
    assert((fakeSpiRecords[0].data==std::vector<uint8_t>{0,0,0,171}));
    size_t bytes=0;
    for(size_t i=1;i<fakeSpiRecords.size();++i){const auto &t=fakeSpiRecords[i];assert(t.cmd==0x32&&t.reg==(i==1?0x2c:0x3c));assert(t.bytes==172*32*2);bytes+=t.bytes;}
    assert(bytes==172*640*2);assert(fakeSpiRecords[1].data[0]==0xf8&&fakeSpiRecords[1].data[1]==0);
    panel.brightness(0);assert(fakeDuty==0);
    puts("panel: half-duplex mode3 QSPI, address window, full-frame continuation and RGB565 byte order passed");
}
