#pragma once
#include "lwip/ip_addr.h"
#include <sys/time.h>
#define LWIP_DHCP_GET_NTP_SRV 1
#define SNTP_MAX_SERVERS 3
enum esp_sntp_operatingmode_t {ESP_SNTP_OPMODE_POLL};
enum sntp_sync_mode_t {SNTP_SYNC_MODE_IMMED};
enum sntp_sync_status_t {SNTP_SYNC_STATUS_RESET,SNTP_SYNC_STATUS_COMPLETED};
bool esp_sntp_enabled();
void esp_sntp_init();
void esp_sntp_stop();
void esp_sntp_setoperatingmode(esp_sntp_operatingmode_t);
void esp_sntp_set_sync_mode(sntp_sync_mode_t);
void esp_sntp_set_sync_interval(uint32_t);
void esp_sntp_set_sync_status(sntp_sync_status_t);
void esp_sntp_servermode_dhcp(bool);
void esp_sntp_setservername(unsigned,const char *);
void esp_sntp_setserver(unsigned,const ip_addr_t *);
extern "C" void sntp_sync_time(struct timeval *);
