#pragma once
#include "fake_runtime.h"
using gpio_num_t=int;
struct sdmmc_host_t {int max_freq_khz=0,slot=1;};
struct sdmmc_slot_config_t {int width=0,clk=-1,cmd=-1,d0=-1;};
struct sdmmc_card_t {};
#define SDMMC_HOST_DEFAULT() sdmmc_host_t{}
#define SDMMC_SLOT_CONFIG_DEFAULT() sdmmc_slot_config_t{}
constexpr int ESP_OK=0;
inline int sdmmc_host_init(){return 0;}
inline int sdmmc_host_init_slot(int,const sdmmc_slot_config_t *s){return s->width==1&&s->clk==41&&s->cmd==39&&s->d0==40?0:-1;}
inline int sdmmc_card_init(const sdmmc_host_t *host,sdmmc_card_t *){return host->max_freq_khz==20000&&fakeCard.present.load()?0:-1;}
inline int sdmmc_host_deinit(){return 0;}
