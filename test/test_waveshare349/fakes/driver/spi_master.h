#pragma once
#include <stdint.h>
#include <stddef.h>
#include <vector>
constexpr int ESP_OK=0,SPI2_HOST=1,SPI_DMA_CH_AUTO=3;
constexpr unsigned SPI_DEVICE_HALFDUPLEX=1,SPI_TRANS_MODE_QIO=2;
struct spi_bus_config_t {int sclk_io_num=0,mosi_io_num=0,miso_io_num=0,quadwp_io_num=0,quadhd_io_num=0;size_t max_transfer_sz=0;};
struct spi_device_interface_config_t {unsigned command_bits=0,address_bits=0,mode=0;int clock_speed_hz=0,spics_io_num=0,queue_size=0;unsigned flags=0;};
using spi_device_handle_t=spi_device_interface_config_t *;
struct spi_transaction_t {unsigned cmd=0;uint32_t addr=0;size_t length=0;const void *tx_buffer=nullptr;unsigned flags=0;};
struct SpiRecord {unsigned cmd,reg;size_t bytes;std::vector<uint8_t> data;};
extern spi_device_interface_config_t fakeSpiConfig;
extern std::vector<SpiRecord> fakeSpiRecords;
inline int spi_bus_initialize(int,const spi_bus_config_t *b,int){return b->sclk_io_num==10&&b->mosi_io_num==11&&b->miso_io_num==12&&b->quadwp_io_num==13&&b->quadhd_io_num==14?0:1;}
inline int spi_bus_add_device(int,const spi_device_interface_config_t *config,spi_device_handle_t *handle){fakeSpiConfig=*config;*handle=&fakeSpiConfig;return 0;}
inline int spi_device_polling_transmit(spi_device_handle_t handle,spi_transaction_t *t){
    // Same IDF 4.4 contract as spi_master.c check_trans_valid: multi-line
    // transactions are rejected when the SPI device is full duplex.
    if((t->flags&SPI_TRANS_MODE_QIO)&&!(handle->flags&SPI_DEVICE_HALFDUPLEX))return 1;
    SpiRecord record{t->cmd,t->addr>>8,t->length/8,{}};
    const auto bytes=static_cast<const uint8_t *>(t->tx_buffer);
    if(record.bytes)record.data.assign(bytes,bytes+record.bytes);
    fakeSpiRecords.push_back(record);return 0;
}
