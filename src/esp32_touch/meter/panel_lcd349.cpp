#ifdef UT_METER_LCD349
#include "../panel.h"
#include "meter_board_devices.h"
#include <waveshare349.h>
#include <Arduino.h>
#include <Wire.h>
#include <driver/spi_master.h>
#include <esp_heap_caps.h>

// Waveshare V1/V2 examples use mode 3, RGB565, 0x02 command/0x32 color
// prefixes and RAMWR/RAMWRC streaming without RASET in QSPI mode. These panels
// have their vendor register table in OTP; do not run the JC3248 register table.
namespace {
constexpr size_t rowsPerTransfer=32,pixelsPerTransfer=172*rowsPerTransfer;
spi_device_handle_t lcd=nullptr;
uint16_t *dmaPixels=nullptr;bool ready=false,touchReady=false;
bool transfer(uint8_t command,const void *data,size_t bytes,bool color=false){
    spi_transaction_t transaction={};transaction.cmd=color?0x32:0x02;
    transaction.addr=uint32_t(command)<<8;transaction.length=bytes*8;transaction.tx_buffer=data;
    if(color)transaction.flags=SPI_TRANS_MODE_QIO;
    return spi_device_polling_transmit(lcd,&transaction)==ESP_OK;
}
}
bool DeskPanel::begin(){
    pinMode(METER_DISPLAY_BACKLIGHT,OUTPUT);digitalWrite(METER_DISPLAY_BACKLIGHT,LOW);
    if(!meter_board::begin()){diagnostic="3.49 auxiliary bus / TCA9554 power init failed";return false;}
    if(!buffer)buffer=static_cast<uint16_t *>(ps_malloc(uint32_t(METER_DISPLAY_WIDTH)*METER_DISPLAY_HEIGHT*2));
    dmaPixels=static_cast<uint16_t *>(heap_caps_malloc(pixelsPerTransfer*2,MALLOC_CAP_DMA));
    if(!buffer||!dmaPixels){diagnostic="3.49 framebuffer/DMA allocation failed";return false;}
    spi_bus_config_t bus={};bus.sclk_io_num=METER_DISPLAY_BUS_SCK;bus.mosi_io_num=METER_DISPLAY_BUS_DATA0;
    bus.miso_io_num=METER_DISPLAY_BUS_DATA1;bus.quadwp_io_num=METER_DISPLAY_BUS_DATA2;bus.quadhd_io_num=METER_DISPLAY_BUS_DATA3;
    bus.max_transfer_sz=pixelsPerTransfer*2;
    if(spi_bus_initialize(SPI2_HOST,&bus,SPI_DMA_CH_AUTO)!=ESP_OK){diagnostic="3.49 QSPI bus init failed";return false;}
    spi_device_interface_config_t config={};config.command_bits=8;config.address_bits=24;config.mode=3;
    config.clock_speed_hz=METER_DISPLAY_FREQUENCY;config.spics_io_num=METER_DISPLAY_BUS_CS;config.queue_size=1;
    config.flags=SPI_DEVICE_HALFDUPLEX; // IDF rejects multi-line data on a full-duplex device.
    if(spi_bus_add_device(SPI2_HOST,&config,&lcd)!=ESP_OK){diagnostic="3.49 QSPI device init failed";return false;}
    bool resetOk=true;
    if(METER_DISPLAY_RESET>=0){
        pinMode(METER_DISPLAY_RESET,OUTPUT);digitalWrite(METER_DISPLAY_RESET,HIGH);delay(30);
        digitalWrite(METER_DISPLAY_RESET,LOW);delay(250);digitalWrite(METER_DISPLAY_RESET,HIGH);delay(30);
    }else{
        resetOk=meter_board::lcdReset(true);delay(30);resetOk=meter_board::lcdReset(false)&&resetOk;delay(250);
        resetOk=meter_board::lcdReset(true)&&resetOk;delay(30);
    }
    if(!resetOk){diagnostic="3.49 expander LCD reset failed";return false;}
    const uint8_t rgb=0,colmod=0x55;
    if(!transfer(0x11,nullptr,0)){diagnostic="3.49 QSPI sleep-out failed";return false;}delay(100);
    if(!transfer(0x36,&rgb,1)||!transfer(0x3a,&colmod,1)||!transfer(0x29,nullptr,0)){
        diagnostic="3.49 QSPI panel init failed";return false;
    }delay(100);
    fillScreen(0);ready=true;flush();
    Wire.begin(METER_TOUCH_SDA,METER_TOUCH_SCL,METER_TOUCH_FREQUENCY);Wire.setTimeOut(20);
    Wire.beginTransmission(METER_TOUCH_ADDRESS);touchReady=Wire.endTransmission()==0;
    ledcSetup(0,5000,8);ledcAttachPin(METER_DISPLAY_BACKLIGHT,0);brightness(55);
    diagnostic=touchReady?"Waveshare 3.49 QSPI / polled touch ready":"3.49 display ready; touch missing on SDA17/SCL18";
    return ready&&touchReady;
}
void DeskPanel::flush(){
    if(!ready||!buffer)return;
    const uint8_t columns[]={0,0,0,171};
    bool ok=transfer(0x2a,columns,4);
    const size_t total=size_t(METER_DISPLAY_WIDTH)*METER_DISPLAY_HEIGHT;
    for(size_t offset=0;ok&&offset<total;offset+=pixelsPerTransfer){
        const size_t count=std::min(pixelsPerTransfer,total-offset);
        for(size_t i=0;i<count;++i){const uint16_t pixel=buffer[offset+i];dmaPixels[i]=uint16_t(pixel<<8|pixel>>8);}
        ok=transfer(offset?0x3c:0x2c,dmaPixels,count*2,true);
    }
    if(!ok){diagnostic="3.49 QSPI frame transfer failed";ready=false;}
}
void DeskPanel::brightness(uint8_t percent){
    if(!ready)return;
    if(!meter_board::backlightEnable(percent>0)){diagnostic="3.49 backlight enable failed";return;}
    ledcWrite(0,uint16_t(percent>100?100:percent)*255/100);
}
bool DeskPanel::touch(uint16_t &x,uint16_t &y,bool &down){
    if(!touchReady)return false;
    static uint32_t lastRead=0;const uint32_t now=millis();if(uint32_t(now-lastRead)<20)return false;lastRead=now;
    static const uint8_t command[]={0xb5,0xab,0xa5,0x5a,0,0,0,0x0e,0,0,0};uint8_t bytes[32];
    Wire.beginTransmission(METER_TOUCH_ADDRESS);Wire.write(command,sizeof(command));
    if(Wire.endTransmission(false)!=0||Wire.requestFrom(uint8_t(METER_TOUCH_ADDRESS),uint8_t(sizeof(bytes)))!=sizeof(bytes)){
        while(Wire.available())Wire.read();
        ++touchErrors;return false;
    }
    Wire.readBytes(bytes,sizeof(bytes));
    if(!waveshare349::decodeTouch(bytes,sizeof(bytes),getRotation(),x,y,down)){++touchErrors;return false;}
    return true;
}
#endif
