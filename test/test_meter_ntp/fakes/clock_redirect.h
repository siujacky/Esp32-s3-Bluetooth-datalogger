#pragma once
// Redirect the system-clock operation so a host test never changes Windows time.
#include <sys/time.h>
extern "C" int meter_ntp_fake_settimeofday(const timeval *,const void *);
#define settimeofday meter_ntp_fake_settimeofday
