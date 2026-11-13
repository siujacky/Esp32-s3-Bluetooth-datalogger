#ifdef METER_NTP_ADAPTER_TEST
#include <unity.h>
#include "../../src/esp32_touch/meter/meter_ntp.h"
#include "fakes/esp_sntp.h"
#include "fakes/freertos/queue.h"
#include "fakes/WiFi.h"
#include "fakes/Preferences.h"
#include <atomic>
#include <chrono>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>
#include <cstring>
#include <cstdio>
#include <stdexcept>

namespace fake {
struct Queue {unsigned capacity;size_t bytes;std::mutex mutex;std::deque<std::vector<uint8_t>> entries;Queue(unsigned cap,size_t len):capacity(cap),bytes(len){}};
std::thread worker;
std::atomic<bool> stopping{false},failReadback{false},failStart{false},flashOnCaller{false},ntpOnCaller{false},network{false};
std::atomic<unsigned> writes{0},clockWrites{0},starts{0};
std::mutex preferencesMutex;
std::vector<uint8_t> record;
const auto boot=std::chrono::steady_clock::now();
std::thread::id caller=std::this_thread::get_id();
void flash(){if(caller==std::this_thread::get_id())flashOnCaller=true;}
void ntp(){if(caller==std::this_thread::get_id())ntpOnCaller=true;}
void finish(){stopping=true;if(worker.joinable())worker.join();}
}
QueueHandle_t xQueueCreate(unsigned cap,size_t size){return new fake::Queue(cap,size);}
int xQueueSend(QueueHandle_t handle,const void *in,unsigned wait){
    if(wait)throw std::runtime_error("NTP queue must never wait");
    auto &q=*static_cast<fake::Queue *>(handle);std::lock_guard<std::mutex> lock(q.mutex);
    if(q.entries.size()>=q.capacity)return 0;
    q.entries.emplace_back(static_cast<const uint8_t *>(in),static_cast<const uint8_t *>(in)+q.bytes);return 1;
}
int xQueueReceive(QueueHandle_t handle,void *out,unsigned wait){
    if(wait)throw std::runtime_error("NTP queue must never wait");
    auto &q=*static_cast<fake::Queue *>(handle);std::lock_guard<std::mutex> lock(q.mutex);
    if(q.entries.empty())return 0;
    memcpy(out,q.entries.front().data(),q.bytes);q.entries.pop_front();return 1;
}
int xQueueOverwrite(QueueHandle_t handle,const void *in){
    auto &q=*static_cast<fake::Queue *>(handle);std::lock_guard<std::mutex> lock(q.mutex);
    if(q.capacity!=1)throw std::runtime_error("overwrite requires a one-item queue");
    q.entries.clear();q.entries.emplace_back(static_cast<const uint8_t *>(in),static_cast<const uint8_t *>(in)+q.bytes);return 1;
}
void xQueueReset(QueueHandle_t handle){auto &q=*static_cast<fake::Queue *>(handle);std::lock_guard<std::mutex> lock(q.mutex);q.entries.clear();}
void vQueueDelete(QueueHandle_t handle){delete static_cast<fake::Queue *>(handle);}
int xTaskCreate(void (*fn)(void *),const char *,unsigned,void *arg,unsigned,void *){fake::worker=std::thread([fn,arg](){try{fn(arg);}catch(const std::runtime_error &){}});return 1;}
void vTaskDelay(unsigned ms){if(fake::stopping)throw std::runtime_error("test shutdown");std::this_thread::sleep_for(std::chrono::milliseconds(ms));}
bool Preferences::begin(const char *space,bool readonly){fake::flash();if(strcmp(space,"meter-ntp")||readonly)throw std::runtime_error("unexpected namespace/mode");return true;}
bool Preferences::isKey(const char *key){fake::flash();if(strcmp(key,"config"))throw std::runtime_error("unexpected key");std::lock_guard<std::mutex> lock(fake::preferencesMutex);return !fake::record.empty();}
size_t Preferences::getBytesLength(const char *){fake::flash();std::lock_guard<std::mutex> lock(fake::preferencesMutex);return fake::record.size();}
size_t Preferences::getBytes(const char *,void *out,size_t size){fake::flash();std::lock_guard<std::mutex> lock(fake::preferencesMutex);if(size!=fake::record.size())return 0;memcpy(out,fake::record.data(),size);if(fake::failReadback)static_cast<uint8_t *>(out)[7]^=1;return size;}
size_t Preferences::putBytes(const char *,const void *in,size_t size){fake::flash();++fake::writes;std::lock_guard<std::mutex> lock(fake::preferencesMutex);fake::record.assign(static_cast<const uint8_t *>(in),static_cast<const uint8_t *>(in)+size);return size;}
FakeWiFi WiFi;
int64_t esp_timer_get_time(){return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-fake::boot).count();}
int ipaddr_aton(const char *in,ip_addr_t *out){return meter_ntp::parseIpv4(in,out->address)?1:0;}
bool esp_sntp_enabled(){fake::ntp();return fake::network;}
void esp_sntp_init(){fake::ntp();fake::network=!fake::failStart;++fake::starts;}
void esp_sntp_stop(){fake::ntp();fake::network=false;}
void esp_sntp_setoperatingmode(esp_sntp_operatingmode_t){fake::ntp();}
void esp_sntp_set_sync_mode(sntp_sync_mode_t){fake::ntp();}
void esp_sntp_set_sync_interval(uint32_t){fake::ntp();}
void esp_sntp_set_sync_status(sntp_sync_status_t){}
void esp_sntp_servermode_dhcp(bool){fake::ntp();}
void esp_sntp_setservername(unsigned,const char *){fake::ntp();}
void esp_sntp_setserver(unsigned,const ip_addr_t *){fake::ntp();}
extern "C" int meter_ntp_fake_settimeofday(const timeval *,const void *){++fake::clockWrites;return 0;}

static uint64_t published=0;
static unsigned publications=0;
static void onSync(uint64_t unixMs){published=unixMs;++publications;}
template<class F>static bool eventually(F predicate){for(unsigned i=0;i<200;++i){if(predicate())return true;std::this_thread::sleep_for(std::chrono::milliseconds(5));}return false;}
static void asynchronous_defaults_wait_for_station_and_do_not_claim_saved(){
    TEST_ASSERT_TRUE(meter_ntp::begin(onSync));
    TEST_ASSERT_TRUE(eventually([](){return meter_ntp::snapshot().ready;}));
    const auto state=meter_ntp::snapshot();TEST_ASSERT_TRUE(state.enabled);TEST_ASSERT_FALSE(state.saved);TEST_ASSERT_TRUE(state.waitingForNetwork);TEST_ASSERT_FALSE(state.synced);TEST_ASSERT_EQUAL_STRING("pool.ntp.org",state.server);TEST_ASSERT_FALSE(fake::flashOnCaller);TEST_ASSERT_FALSE(fake::ntpOnCaller);
}
static void asynchronous_persistence_does_not_acknowledge_corrupt_readback(){
    String error;fake::failReadback=true;
    TEST_ASSERT_TRUE(meter_ntp::configure(false,"ntp.local",error));
    TEST_ASSERT_TRUE(eventually([](){return !meter_ntp::snapshot().pending;}));
    auto state=meter_ntp::snapshot();TEST_ASSERT_FALSE(state.enabled);TEST_ASSERT_FALSE(state.saved);TEST_ASSERT_TRUE(strlen(state.error)>0);
    fake::failReadback=false;TEST_ASSERT_TRUE(meter_ntp::configure(false,"ntp.local",error));TEST_ASSERT_TRUE(eventually([](){return !meter_ntp::snapshot().pending;}));
    state=meter_ntp::snapshot();TEST_ASSERT_TRUE(state.saved);TEST_ASSERT_EQUAL_STRING("",state.error);TEST_ASSERT_EQUAL_UINT(2,fake::writes.load());TEST_ASSERT_FALSE(fake::flashOnCaller);
}
static void local_softap_reply_is_validated_before_setting_clock_and_delivered_from_tick(){
    String error;TEST_ASSERT_TRUE(meter_ntp::configure(true,"192.168.4.2",error));
    TEST_ASSERT_TRUE(eventually([](){return meter_ntp::snapshot().active;}));
    TEST_ASSERT_FALSE(meter_ntp::snapshot().waitingForNetwork);
    timeval invalid={1577836799,0};sntp_sync_time(&invalid);
    TEST_ASSERT_TRUE(eventually([](){return strlen(meter_ntp::snapshot().error)>0;}));TEST_ASSERT_EQUAL_UINT(0,fake::clockWrites.load());TEST_ASSERT_FALSE(meter_ntp::snapshot().synced);
    timeval valid={1794480000,123456};sntp_sync_time(&valid);
    TEST_ASSERT_TRUE(eventually([](){return meter_ntp::snapshot().synced;}));
    auto state=meter_ntp::snapshot();TEST_ASSERT_EQUAL_UINT64(1794480000123ULL,state.lastSyncUnixMs);TEST_ASSERT_EQUAL_STRING("192.168.4.2",state.lastSyncServer);TEST_ASSERT_EQUAL_UINT(1,fake::clockWrites.load());TEST_ASSERT_EQUAL_UINT(0,publications);
    meter_ntp::tick(false);TEST_ASSERT_EQUAL_UINT(1,publications);TEST_ASSERT_TRUE(published>=1794480000123ULL&&published<1794480001123ULL);TEST_ASSERT_EQUAL_STRING("",state.error);
}
static void disable_and_station_loss_preserve_historical_sync_and_stop_requests(){
    String error;TEST_ASSERT_TRUE(meter_ntp::configure(false,"pool.ntp.org",error));TEST_ASSERT_TRUE(eventually([](){return !meter_ntp::snapshot().pending;}));
    const unsigned before=fake::starts;TEST_ASSERT_TRUE(meter_ntp::requestSync());TEST_ASSERT_TRUE(eventually([](){return !meter_ntp::snapshot().pending;}));
    auto state=meter_ntp::snapshot();TEST_ASSERT_FALSE(state.active);TEST_ASSERT_TRUE(state.synced);TEST_ASSERT_TRUE(strlen(state.error)>0);TEST_ASSERT_EQUAL_UINT(before,fake::starts.load());
    TEST_ASSERT_TRUE(meter_ntp::configure(true,"pool.ntp.org",error));TEST_ASSERT_TRUE(eventually([](){return !meter_ntp::snapshot().pending;}));TEST_ASSERT_TRUE(meter_ntp::snapshot().waitingForNetwork);
    meter_ntp::tick(true);TEST_ASSERT_TRUE(eventually([](){return meter_ntp::snapshot().active;}));
    meter_ntp::tick(false);TEST_ASSERT_TRUE(eventually([](){return !meter_ntp::snapshot().active;}));state=meter_ntp::snapshot();TEST_ASSERT_TRUE(state.waitingForNetwork);TEST_ASSERT_TRUE(state.synced);TEST_ASSERT_FALSE(fake::ntpOnCaller);
}
static void failed_sntp_start_is_reported_and_manual_retry_can_recover(){
    String error;fake::failStart=true;TEST_ASSERT_TRUE(meter_ntp::configure(true,"192.168.4.2",error));
    TEST_ASSERT_TRUE(eventually([](){return !meter_ntp::snapshot().pending;}));
    TEST_ASSERT_TRUE(eventually([](){const auto state=meter_ntp::snapshot();return !state.active&&strlen(state.error)>0;}));
    TEST_ASSERT_FALSE(meter_ntp::snapshot().syncing);
    fake::failStart=false;TEST_ASSERT_TRUE(meter_ntp::requestSync());TEST_ASSERT_TRUE(eventually([](){return meter_ntp::snapshot().active;}));
}
void runAdapterTests(){RUN_TEST(asynchronous_defaults_wait_for_station_and_do_not_claim_saved);RUN_TEST(asynchronous_persistence_does_not_acknowledge_corrupt_readback);RUN_TEST(local_softap_reply_is_validated_before_setting_clock_and_delivered_from_tick);RUN_TEST(disable_and_station_loss_preserve_historical_sync_and_stop_requests);RUN_TEST(failed_sntp_start_is_reported_and_manual_retry_can_recover);fake::finish();}
#endif

