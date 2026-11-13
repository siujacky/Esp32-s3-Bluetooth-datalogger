#pragma once
#include <cstdint>
struct ip_addr_t {uint32_t address;};
int ipaddr_aton(const char *,ip_addr_t *);
