#pragma once
// Host-only card/task fixtures. They never access a device or host filesystem.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
using String=std::string;
inline size_t strlcpy(char *out,const char *in,size_t capacity) {
    const size_t size=std::strlen(in);
    if(capacity){const size_t n=std::min(size,capacity-1);std::memcpy(out,in,n);out[n]=0;}
    return size;
}
inline uint32_t millis() {
    static const auto start=std::chrono::steady_clock::now();
    return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count());
}
using TickType_t=uint32_t;
constexpr int pdTRUE=1,pdFALSE=0,pdPASS=1;
#define pdMS_TO_TICKS(n) (n)
using portMUX_TYPE=std::recursive_mutex;
#define portMUX_INITIALIZER_UNLOCKED {}
extern thread_local bool pauseOnCriticalExit;
extern std::atomic<bool> criticalExitPaused,releaseCriticalExit;
inline void fakeCriticalExit(portMUX_TYPE *mux) {
    mux->unlock();
    if(pauseOnCriticalExit) {
        pauseOnCriticalExit=false;criticalExitPaused.store(true);
        while(!releaseCriticalExit.load())std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
#define portENTER_CRITICAL(mux) (mux)->lock()
#define portEXIT_CRITICAL(mux) fakeCriticalExit(mux)
using SemaphoreHandle_t=std::timed_mutex*;
inline SemaphoreHandle_t xSemaphoreCreateMutex(){return new std::timed_mutex;}
inline int xSemaphoreTake(SemaphoreHandle_t lock,TickType_t wait){return lock->try_lock_for(std::chrono::milliseconds(wait))?pdTRUE:pdFALSE;}
inline void xSemaphoreGive(SemaphoreHandle_t lock){lock->unlock();}
struct FakeQueue {
    size_t limit,itemBytes;std::mutex mutex;std::condition_variable ready;
    std::deque<std::vector<uint8_t>> items;
    FakeQueue(size_t depth,size_t bytes):limit(depth),itemBytes(bytes){}
};
using QueueHandle_t=FakeQueue*;
extern std::atomic<bool> pauseQueueReceive,queueReceivePaused;
inline QueueHandle_t xQueueCreate(size_t depth,size_t bytes){return new FakeQueue(depth,bytes);}
inline int xQueueSend(QueueHandle_t queue,const void *item,TickType_t) {
    std::lock_guard<std::mutex> guard(queue->mutex);
    if(queue->items.size()==queue->limit)return pdFALSE;
    const auto bytes=static_cast<const uint8_t*>(item);
    queue->items.emplace_back(bytes,bytes+queue->itemBytes);queue->ready.notify_one();return pdTRUE;
}
inline int xQueueReceive(QueueHandle_t queue,void *item,TickType_t wait) {
    if(pauseQueueReceive.load()) {
        queueReceivePaused.store(true);
        while(pauseQueueReceive.load())std::this_thread::sleep_for(std::chrono::milliseconds(1));
        queueReceivePaused.store(false);
    }
    std::unique_lock<std::mutex> guard(queue->mutex);
    if(!queue->ready.wait_for(guard,std::chrono::milliseconds(wait),[&]{return !queue->items.empty();}))return pdFALSE;
    std::memcpy(item,queue->items.front().data(),queue->itemBytes);queue->items.pop_front();return pdTRUE;
}
using TaskHandle_t=void*;
inline int xTaskCreatePinnedToCore(void (*fn)(void*),const char *,unsigned,void *arg,unsigned,TaskHandle_t *task,int) {
    *task=reinterpret_cast<void*>(1);std::thread([=]{fn(arg);}).detach();return pdPASS;
}
inline void vTaskDelay(TickType_t wait){std::this_thread::sleep_for(std::chrono::milliseconds(wait));}
inline void taskYIELD(){std::this_thread::yield();}
constexpr int HSPI=3,CARD_NONE=0,CARD_SDHC=3;
struct SPIClass {
    static int bus,clock,miso,mosi,cs;
    explicit SPIClass(int id){bus=id;}
    void begin(int sck,int in,int out,int select){clock=sck;miso=in;mosi=out;cs=select;}
    void end(){}
};
constexpr const char *FILE_READ="r",*FILE_WRITE="w";
struct FakeNode {std::string name,data;bool directory=false;};
struct FakeHandle {std::shared_ptr<FakeNode> node;size_t position=0,directoryIndex=0;int error=0;};
struct FakeCard {
    std::mutex mutex;
    std::map<std::string,std::shared_ptr<FakeNode>> nodes;
    std::atomic<bool> present{false},pauseWrites{false},writeBlocked{false},failNextWrite{false};
    std::atomic<bool> failFlush{false},failSync{false},failClose{false};
    std::atomic<bool> unformatted{false},failMkfs{false},failFormatRemount{false},formatCompleted{false};
    std::atomic<bool> pauseMkfs{false},mkfsBlocked{false};
    std::atomic<unsigned> formatCalls{0},lowLevelInitCalls{0};
    uint8_t allocatedDrive=1,registeredDrive=0xff;
    std::string lastFormatDrive,lastDetachedDrive;
    unsigned lastFormatFlags=0;
    std::string flashMarker="internal-flash-preserved";
    std::atomic<bool> internalVolumeMounted{true};
    bool formatRequested=false;
    uint64_t capacity=16*1024*1024;
    std::shared_ptr<FakeNode> add(const std::string &name,const std::string &data="",bool directory=false) {
        auto node=std::make_shared<FakeNode>();node->name=name;node->data=data;node->directory=directory;nodes[name]=node;return node;
    }
    uint64_t used() const {uint64_t out=0;for(const auto &n:nodes)out+=n.second->data.size();return out;}
};
extern FakeCard fakeCard;
class File {
    std::shared_ptr<FakeHandle> handle_;
public:
    File()=default;
    explicit File(std::shared_ptr<FakeNode> node):handle_(std::make_shared<FakeHandle>()){handle_->node=node;}
    explicit operator bool() const{return handle_&&handle_->node;}
    bool isDirectory() const{return *this&&handle_->node->directory;}
    const char *name() const{return handle_->node->name.c_str();}
    uint64_t size() const{std::lock_guard<std::mutex> guard(fakeCard.mutex);return *this?handle_->node->data.size():0;}
    size_t write(const uint8_t *bytes,size_t count) {
        if(fakeCard.pauseWrites.load()) {
            fakeCard.writeBlocked.store(true);
            while(fakeCard.pauseWrites.load())std::this_thread::sleep_for(std::chrono::milliseconds(1));
            fakeCard.writeBlocked.store(false);
        }
        std::lock_guard<std::mutex> guard(fakeCard.mutex);
        if(!*this||!fakeCard.present.load()||fakeCard.failNextWrite.exchange(false)){if(*this)handle_->error=1;return 0;}
        const uint64_t used=fakeCard.used();
        count=static_cast<size_t>(std::min<uint64_t>(count,fakeCard.capacity>used?fakeCard.capacity-used:0));
        handle_->node->data.append(reinterpret_cast<const char*>(bytes),count);return count;
    }
    size_t read(uint8_t *bytes,size_t count) {
        std::lock_guard<std::mutex> guard(fakeCard.mutex);
        if(!*this||!fakeCard.present.load())return 0;
        const auto &data=handle_->node->data;
        count=std::min(count,data.size()-std::min(data.size(),handle_->position));
        std::memcpy(bytes,data.data()+handle_->position,count);handle_->position+=count;return count;
    }
    int getWriteError()const{return *this?handle_->error:0;}
    void flush(){}
    void close(){handle_.reset();}
    File openNextFile() {
        std::lock_guard<std::mutex> guard(fakeCard.mutex);
        std::vector<std::shared_ptr<FakeNode>> entries;
        const std::string prefix=handle_->node->name+"/";
        for(const auto &n:fakeCard.nodes)if(n.first.compare(0,prefix.size(),prefix)==0&&n.first.find('/',prefix.size())==std::string::npos)entries.push_back(n.second);
        return handle_->directoryIndex<entries.size()?File(entries[handle_->directoryIndex++]):File();
    }
};
class FakeSD {
public:
    bool begin(int,SPIClass &,unsigned,const char *,unsigned,bool format) {
        fakeCard.formatRequested=format;
        if(fakeCard.formatCompleted.exchange(false)&&fakeCard.failFormatRemount.exchange(false))return false;
        return fakeCard.present.load()&&!fakeCard.unformatted.load();
    }
    void end(){}
    int cardType()const{return fakeCard.present.load()?CARD_SDHC:CARD_NONE;}
    uint64_t cardSize()const{std::lock_guard<std::mutex> guard(fakeCard.mutex);return fakeCard.capacity;}
    uint64_t totalBytes()const{return cardSize();}
    uint64_t usedBytes()const{std::lock_guard<std::mutex> guard(fakeCard.mutex);return fakeCard.used();}
    bool exists(const String &path)const{std::lock_guard<std::mutex> guard(fakeCard.mutex);return fakeCard.nodes.count(path)!=0;}
    bool mkdir(const char *path){std::lock_guard<std::mutex> guard(fakeCard.mutex);fakeCard.add(path,"",true);return true;}
    File open(const String &path,const char *mode) {
        std::lock_guard<std::mutex> guard(fakeCard.mutex);
        if(!fakeCard.present.load())return {};
        auto node=fakeCard.nodes.find(path);
        if(mode==FILE_WRITE) {
            if(node==fakeCard.nodes.end())return File(fakeCard.add(path));
            node->second->data.clear();return File(node->second);
        }
        return node==fakeCard.nodes.end()?File():File(node->second);
    }
};
extern FakeSD SD;
