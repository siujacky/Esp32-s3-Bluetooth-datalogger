#include "meter_storage.h"

#if (defined(METER_STORAGE_ENABLED) && METER_STORAGE_ENABLED) || (!defined(METER_BOARD_CONFIGURED) && defined(UT_METER_JC3248))
#include <meter_log_format.h>
#include <meter_log_io.h>
#if METER_STORAGE_SDMMC
#include <SD_MMC.h>
#include <driver/sdmmc_host.h>
#include <sdmmc_cmd.h>
#include <diskio_impl.h>
#include <diskio_sdmmc.h>
#else
#include <SD.h>
#include <sd_diskio.h>
#endif
#include <ff.h>
#include <SPI.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <new>
#include <cstdio>
#include <memory>

namespace meter_storage {
namespace {
#if METER_STORAGE_SDMMC
auto &cardFS=SD_MMC;
#else
auto &cardFS=SD;
#endif
constexpr unsigned kQueueDepth=32,kFileLimit=128,kChunkBytes=1024,kControlSlots=8;
#ifdef METER_BOARD_CONFIGURED
constexpr int kCardCs=METER_STORAGE_CS,kCardMosi=METER_STORAGE_MOSI,kCardClock=METER_STORAGE_SCK,kCardMiso=METER_STORAGE_MISO;
constexpr uint32_t kCardFrequency=METER_STORAGE_FREQUENCY;
#else
constexpr int kCardCs=10,kCardMosi=11,kCardClock=12,kCardMiso=13;
constexpr uint32_t kCardFrequency=4000000;
#endif
constexpr TickType_t kLockWait=pdMS_TO_TICKS(35);
constexpr const char *kDirectory="/meter-logs";
constexpr const char *kMountPoint="/meter-tf";
struct QueuedRecord {Record record;uint32_t droppedBefore=0;};
struct FileEntry {char name[48]={};uint64_t bytes=0;bool active=false;};
struct State {
    bool mounted=false,recording=false,listTruncated=false,formatting=false;
    uint64_t cardBytes=0,totalBytes=0,usedBytes=0;
    char activeFile[48]={},error[160]={};
    char formatResult[12]="idle",formatMessage[160]={},formatOperationId[65]={};
    uint32_t formatGeneration=0;
};
State g_state;
portMUX_TYPE g_stateLock=portMUX_INITIALIZER_UNLOCKED;
QueueHandle_t g_queue=nullptr;
SemaphoreHandle_t g_fsMutex=nullptr;
TaskHandle_t g_task=nullptr;
#if !METER_STORAGE_SDMMC
SPIClass g_spi(HSPI); // SPI3: the display owns SPI2/QSPI.
#endif
FILE *g_writer=nullptr;
File g_download;
FileEntry g_files[kFileLimit];
unsigned g_fileCount=0,g_rowsSinceFlush=0;
uint32_t g_lastFlush=0,g_lastSpaceCheck=0,g_tokenCounter=0,g_downloadToken=0;
uint64_t g_downloadRemaining=0;
uint64_t g_writerBytes=0;
char g_activeSession[40]={};
struct SessionControl {Record stop;bool used=false,haveStop=false;};
SessionControl g_controls[kControlSlots];
#if !METER_STORAGE_SDMMC
bool g_spiBegun=false;
#endif
std::atomic<bool> g_started{false},g_inputActive{false},g_remounting{false},g_remountRequested{false};
std::atomic<bool> g_formatting{false},g_formatRequested{false};
std::atomic<uint32_t> g_dropped{0},g_written{0},g_buffered{0},g_pending{0},g_downloads{0},g_closeToken{0};
char g_formatHistory[8][65]={};
unsigned g_formatHistoryNext=0;

template<size_t N> void textCopy(char (&out)[N],const char *in) {strlcpy(out,in?in:"",N);}
template<size_t N> std::string bounded(const char (&text)[N]) {return std::string(text,strnlen(text,N));}
void errorState(const char *message) {
    portENTER_CRITICAL(&g_stateLock);textCopy(g_state.error,message);portEXIT_CRITICAL(&g_stateLock);
}
State snapshot() {
    portENTER_CRITICAL(&g_stateLock);State state=g_state;portEXIT_CRITICAL(&g_stateLock);return state;
}
// Caller holds the short state lock. The current operation retains its result;
// other recently accepted IDs are rejected without replaying the erase.
int formatReplayLocked(const char *id) {
    if(std::strcmp(g_state.formatOperationId,id)==0)return 1;
    for(unsigned i=0;i<8;++i)if(std::strcmp(g_formatHistory[i],id)==0)return -1;
    return 0;
}
bool reserveControl(const Record &record) {
    bool reserved=false;
    portENTER_CRITICAL(&g_stateLock);
    for(unsigned i=0;i<kControlSlots;++i)if(!g_controls[i].used) {
        g_controls[i].stop=Record{};
        std::memcpy(g_controls[i].stop.sessionId,record.sessionId,sizeof(record.sessionId));
        g_controls[i].stop.sessionId[sizeof(record.sessionId)-1]=0;
        g_controls[i].used=true;g_controls[i].haveStop=false;reserved=true;break;
    }
    portEXIT_CRITICAL(&g_stateLock);return reserved;
}
void releaseControl(const char *session) {
    if(!session[0])return;
    portENTER_CRITICAL(&g_stateLock);
    for(unsigned i=0;i<kControlSlots;++i)if(g_controls[i].used&&std::strncmp(g_controls[i].stop.sessionId,session,sizeof(g_activeSession))==0) {
        g_controls[i].used=false;g_controls[i].haveStop=false;
    }
    portEXIT_CRITICAL(&g_stateLock);
}
void retainStop(const Record &record) {
    portENTER_CRITICAL(&g_stateLock);
    for(unsigned i=0;i<kControlSlots;++i)if(g_controls[i].used&&std::strncmp(g_controls[i].stop.sessionId,record.sessionId,sizeof(record.sessionId))==0) {
        g_controls[i].stop=record;g_controls[i].haveStop=true;break;
    }
    portEXIT_CRITICAL(&g_stateLock);
}
bool lockFs() {return g_fsMutex&&xSemaphoreTake(g_fsMutex,kLockWait)==pdTRUE;}
void unlockFs() {xSemaphoreGive(g_fsMutex);}
void updateEntry(const char *name,uint64_t bytes,bool active) {
    unsigned index=0;
    for(;index<g_fileCount;++index)if(std::strcmp(g_files[index].name,name)==0)break;
    if(index==g_fileCount) {
        if(g_fileCount==kFileLimit) {
            for(unsigned i=1;i<kFileLimit;++i)g_files[i-1]=g_files[i];
            index=kFileLimit-1;
            portENTER_CRITICAL(&g_stateLock);g_state.listTruncated=true;portEXIT_CRITICAL(&g_stateLock);
        } else ++g_fileCount;
    }
    textCopy(g_files[index].name,name);g_files[index].bytes=bytes;g_files[index].active=active;
}
void refreshSpace() {
    const uint64_t card=cardFS.cardSize(),total=cardFS.totalBytes(),used=cardFS.usedBytes();
    portENTER_CRITICAL(&g_stateLock);
    g_state.cardBytes=card;g_state.totalBytes=total;g_state.usedBytes=used;
    portEXIT_CRITICAL(&g_stateLock);g_lastSpaceCheck=millis();
}
bool flushWriter() {
    if(!g_writer)return false;
    const meter_log_io::FlushResult result=meter_log_io::checkedFlush(g_writer);
    if(result!=meter_log_io::FlushResult::Ok) {
        g_dropped.fetch_add(g_buffered.exchange(0));g_rowsSinceFlush=0;
        errorState(result==meter_log_io::FlushResult::SyncError?
            "TF fsync failed; buffered records are unconfirmed and recording stopped":
            "TF flush failed; buffered records are unconfirmed and recording stopped");
        portENTER_CRITICAL(&g_stateLock);g_state.recording=false;portEXIT_CRITICAL(&g_stateLock);
        return false;
    }
    const uint32_t persisted=g_buffered.exchange(0);g_written.fetch_add(persisted);
    g_lastFlush=millis();g_rowsSinceFlush=0;
    if(persisted) {portENTER_CRITICAL(&g_stateLock);g_state.recording=true;portEXIT_CRITICAL(&g_stateLock);}
    return true;
}
void closeWriter(bool tryFlush=true) {
    if(g_writer) {
        const bool flushed=tryFlush&&flushWriter();
        if(!tryFlush)g_dropped.fetch_add(g_buffered.exchange(0));
        const State state=snapshot();
        if(fclose(g_writer)!=0&&flushed)errorState("TF close failed after sync; recording stopped");
        g_writer=nullptr;
        updateEntry(state.activeFile,g_writerBytes,false);
    }
    releaseControl(g_activeSession);g_activeSession[0]=0;g_rowsSinceFlush=0;g_writerBytes=0;
    portENTER_CRITICAL(&g_stateLock);g_state.recording=false;g_state.activeFile[0]=0;portEXIT_CRITICAL(&g_stateLock);
}
void closeReader() {
    if(g_download)g_download.close();
    g_downloadToken=0;g_downloadRemaining=0;g_downloads.store(0);
}
void scanFiles() {
    g_fileCount=0;
    portENTER_CRITICAL(&g_stateLock);g_state.listTruncated=false;portEXIT_CRITICAL(&g_stateLock);
    File dir=cardFS.open(kDirectory,FILE_READ);
    if(!dir||!dir.isDirectory()) {errorState("Unable to read /meter-logs directory");return;}
    unsigned scanned=0;const uint32_t start=millis();
    for(;;) {
        File file=dir.openNextFile();
        if(!file)break;
        const char *name=file.name(),*slash=std::strrchr(name,'/');
        if(slash)name=slash+1;
        if(!file.isDirectory()&&meter_log_format::validLogName(name))updateEntry(name,file.size(),false);
        file.close();++scanned;
        // Bounded metadata work even on a card with unrelated directory data.
        if(g_fileCount==kFileLimit||scanned>=256||millis()-start>750) {
            portENTER_CRITICAL(&g_stateLock);g_state.listTruncated=true;portEXIT_CRITICAL(&g_stateLock);break;
        }
        taskYIELD();
    }
    dir.close();
}
void mountCard() {
    closeWriter();closeReader();
    cardFS.end();
#if !METER_STORAGE_SDMMC
    if(g_spiBegun)g_spi.end();
    g_spi.begin(kCardClock,kCardMiso,kCardMosi,kCardCs);g_spiBegun=true;
#endif
    portENTER_CRITICAL(&g_stateLock);
    g_state.mounted=false;g_state.cardBytes=0;g_state.totalBytes=0;g_state.usedBytes=0;
    portEXIT_CRITICAL(&g_stateLock);g_fileCount=0;
    // Last argument is explicitly false: a failed FAT mount never formats.
#if METER_STORAGE_SDMMC
    const bool mounted=cardFS.setPins(METER_STORAGE_CLK,METER_STORAGE_CMD,METER_STORAGE_D0)&&
        cardFS.begin(kMountPoint,true,false,kCardFrequency/1000,5);
#else
    const bool mounted=cardFS.begin(kCardCs,g_spi,kCardFrequency,kMountPoint,5,false);
#endif
    if(!mounted||cardFS.cardType()==CARD_NONE) {
        cardFS.end();errorState("TF card missing or mount failed; insert a FAT card and remount");return;
    }
    if(!cardFS.exists(kDirectory)&&!cardFS.mkdir(kDirectory)) {errorState("Unable to create /meter-logs; card may be full or read-only");return;}
    File dir=cardFS.open(kDirectory,FILE_READ);
    const bool isDirectory=dir&&dir.isDirectory();dir.close();
    if(!isDirectory) {errorState("/meter-logs is not a readable directory");return;}
    portENTER_CRITICAL(&g_stateLock);g_state.mounted=true;g_state.error[0]=0;portEXIT_CRITICAL(&g_stateLock);
    refreshSpace();scanFiles();
}
void finishFormat(bool success,const char *message) {
    portENTER_CRITICAL(&g_stateLock);
    textCopy(g_state.formatResult,success?"success":"failed");textCopy(g_state.formatMessage,message);
    if(!success)textCopy(g_state.error,message);
    g_state.formatting=false;g_formatting.store(false);
    portEXIT_CRITICAL(&g_stateLock);
}
void formatCard() {
    portENTER_CRITICAL(&g_stateLock);
    textCopy(g_state.formatResult,"running");textCopy(g_state.formatMessage,"Formatting TF card as FAT/FAT32");
    portEXIT_CRITICAL(&g_stateLock);
    // The accepting state is synchronized with append(). Check again before
    // any filesystem teardown; formatting must never steal a live handle.
    if(g_inputActive.load()||g_pending.load()||g_buffered.load()||g_writer||g_downloads.load()||g_remounting.load()) {
        finishFormat(false,"TF format cancelled because card users are active");return;
    }
    std::unique_ptr<BYTE[]> work(new(std::nothrow) BYTE[FF_MAX_SS]);
    if(!work) {finishFormat(false,"TF format failed: unable to allocate the FAT work buffer");return;}
    cardFS.end();
#if !METER_STORAGE_SDMMC
    if(g_spiBegun)g_spi.end();
    g_spi.begin(kCardClock,kCardMiso,kCardMosi,kCardCs);g_spiBegun=true;
#endif
    portENTER_CRITICAL(&g_stateLock);
    g_state.mounted=false;g_state.listTruncated=false;
    g_state.cardBytes=0;g_state.totalBytes=0;g_state.usedBytes=0;
    portEXIT_CRITICAL(&g_stateLock);g_fileCount=0;

#if METER_STORAGE_SDMMC
    // Register only this physical SDMMC card after the normal mount has ended.
    // Obtain a free drive dynamically; never assume drive zero or format flash.
    sdmmc_host_t host=SDMMC_HOST_DEFAULT();host.max_freq_khz=kCardFrequency/1000;
    sdmmc_slot_config_t slot=SDMMC_SLOT_CONFIG_DEFAULT();slot.width=1;
    slot.clk=gpio_num_t(METER_STORAGE_CLK);slot.cmd=gpio_num_t(METER_STORAGE_CMD);slot.d0=gpio_num_t(METER_STORAGE_D0);
    sdmmc_card_t card={};BYTE drive=FF_DRV_NOT_USED;
    if(sdmmc_host_init()!=ESP_OK){finishFormat(false,"TF format: SDMMC host unavailable");return;}
    if(sdmmc_host_init_slot(host.slot,&slot)!=ESP_OK||sdmmc_card_init(&host,&card)!=ESP_OK||
       ff_diskio_get_drive(&drive)!=ESP_OK||drive>=FF_VOLUMES||drive>=10){
        sdmmc_host_deinit();finishFormat(false,"TF format: SDMMC card/drive registration failed");return;
    }
#if FF_MULTI_PARTITION
    if(VolToPart[drive].pd!=drive||VolToPart[drive].pt!=0){
        sdmmc_host_deinit();finishFormat(false,"TF format refused: SDMMC logical drive mismatch");return;
    }
#endif
    ff_diskio_register_sdmmc(drive,&card);
    if(ff_diskio_get_pdrv_card(&card)!=drive){
        ff_diskio_unregister(drive);sdmmc_host_deinit();finishFormat(false,"TF format refused: SDMMC drive identity mismatch");return;
    }
    char fatDrive[3]={static_cast<char>('0'+drive),':',0};
    const FRESULT formatted=f_mkfs(fatDrive,FM_FAT|FM_FAT32,0,work.get(),FF_MAX_SS);
    ff_diskio_unregister(drive);
    if(sdmmc_host_deinit()!=ESP_OK){finishFormat(false,"TF format ended but SDMMC cleanup failed");return;}
#else
    // Allocate a fresh diskio registration specifically for this CS10/HSPI
    // card. Never assume drive 0: it may belong to an internal flash volume.
    const uint8_t drive=sdcard_init(kCardCs,&g_spi,4000000);
    if(drive==0xff||drive>=FF_VOLUMES||drive>=10) {
        finishFormat(false,"TF format failed: card SPI drive registration unavailable");return;
    }
#if FF_MULTI_PARTITION
    if(VolToPart[drive].pd!=drive||VolToPart[drive].pt!=0) {
        // No FAT volume was mounted by this fresh registration. In the rejected
        // mapping, sdcard_unmount would detach a different disk's logical volume.
        // Release only the physical SPI disk registration that we own.
        sdcard_uninit(drive);
        finishFormat(false,"TF format refused: logical drive does not match the card SPI drive");return;
    }
#endif
    char fatDrive[3]={static_cast<char>('0'+drive),':',0};
    const FRESULT detached=f_mount(nullptr,fatDrive,0);
    if(detached!=FR_OK) {
        sdcard_unmount(drive);sdcard_uninit(drive);
        finishFormat(false,"TF format failed: unable to detach the card FAT volume");return;
    }
    // f_mkfs initializes the registered SPI disk even when its existing card
    // filesystem cannot mount. Only this explicit operation calls f_mkfs.
    const FRESULT formatted=f_mkfs(fatDrive,FM_FAT|FM_FAT32,0,work.get(),FF_MAX_SS);
    const uint8_t unmounted=sdcard_unmount(drive),uninitialized=sdcard_uninit(drive);
    if(unmounted||uninitialized) {finishFormat(false,"TF format ended but card drive cleanup failed; remount required");return;}
#endif
    mountCard(); // Normal no-format mount; never retry the erase on failure.
    const State state=snapshot();
    if(formatted!=FR_OK) {
        char message[160];snprintf(message,sizeof(message),"TF format failed (FatFs %u); card data may be incomplete%s",static_cast<unsigned>(formatted),state.mounted?"":"; remount failed");
        finishFormat(false,message);return;
    }
    if(!state.mounted||state.error[0]) {finishFormat(false,"TF format completed, but remount/log-directory setup failed; card data was erased");return;}
    finishFormat(true,"TF card formatted and mounted as FAT/FAT32");
}
// Called only under the filesystem mutex. Recheck after queue receive as a
// remount can be accepted while the worker is waiting for the next record.
void performRemount() {
    if(g_remountRequested.exchange(false)) {mountCard();g_remounting.store(false);}
}
void performMaintenance() {
    if(g_formatRequested.exchange(false))formatCard();
    else performRemount();
}
bool startEvent(const Record &record) {
    return !record.reading&&!record.stopSession&&std::strncmp(record.event,"recording",sizeof(record.event))==0&&std::strncmp(record.message,"Recording started",sizeof(record.message))==0;
}
bool startWriter(const Record &record) {
    closeWriter();
    if(!snapshot().mounted) {errorState("TF card unavailable; browser recording can continue");return false;}
    const std::string name=meter_log_format::fileName(bounded(record.sessionId));
    if(name.empty()) {errorState("Invalid generated TF session ID");return false;}
    const String path=String(kDirectory)+"/"+name.c_str();
    if(cardFS.exists(path)) {errorState("TF session file already exists; refusing to overwrite");return false;}
    refreshSpace();
    const State space=snapshot();
    if(space.totalBytes<=space.usedBytes||space.totalBytes-space.usedBytes<4096) {errorState("TF card full; recording was not started");return false;}
    // fopen's exclusive-create flag is another guard against truncation. The
    // native VFS path includes the exact mount prefix supplied to cardFS.begin().
    const String nativePath=String(kMountPoint)+path;
    g_writer=fopen(nativePath.c_str(),"wbx");
    if(!g_writer) {errorState("Unable to create TF log; card missing, full or read-only");return false;}
    const std::string header=meter_log_format::csvHeader();
    const size_t written=fwrite(header.data(),1,header.size(),g_writer);
    if(written!=header.size()||ferror(g_writer)) {fclose(g_writer);g_writer=nullptr;errorState("TF header write failed; card may be full or missing");return false;}
    g_writerBytes=header.size();
    if(!flushWriter()) {fclose(g_writer);g_writer=nullptr;g_writerBytes=0;return false;}
    textCopy(g_activeSession,bounded(record.sessionId).c_str());
    portENTER_CRITICAL(&g_stateLock);
    textCopy(g_state.activeFile,name.c_str());g_state.recording=false;
    portEXIT_CRITICAL(&g_stateLock);
    updateEntry(name.c_str(),g_writerBytes,true);g_lastFlush=millis();return true;
}
meter_log_format::Row rowFor(const QueuedRecord &queued) {
    const Record &r=queued.record;const meter_web::Sample &s=r.sample;
    meter_log_format::Row row;
    row.seq=r.seq;row.timestampMs=r.timestampMs;row.uptimeMs=s.uptimeMs;row.droppedBefore=queued.droppedBefore;
    row.packet=s.packet;row.modeKey=s.modeKey;row.flags=s.flags;row.ruleId=r.ruleId;
    row.meter=s.meter;row.channel=s.channel;row.code=r.code;row.value=s.value;
    row.numeric=s.numeric;row.reading=r.reading;row.success=r.success;
    row.deviceId=bounded(r.deviceId);row.bootId=bounded(r.bootId);row.sessionId=bounded(r.sessionId);row.label=bounded(r.label);
    row.event=bounded(r.event);row.message=bounded(r.message);row.address=bounded(s.address);
    row.profile=bounded(s.profile);row.text=bounded(s.text);row.unit=bounded(s.unit);row.mode=bounded(s.mode);
    return row;
}
void writeRecord(const QueuedRecord &queued) {
    const Record &record=queued.record;
    const std::string session=bounded(record.sessionId);
    if(!g_writer||session!=g_activeSession) {
        ++g_dropped;
        if(snapshot().error[0]==0)errorState("TF session start was not written; tagged records are being dropped");
        return;
    }
    const std::string line=meter_log_format::csvRow(rowFor(queued));
    if(millis()-g_lastSpaceCheck>=5000)refreshSpace();
    const State space=snapshot();
    if(space.totalBytes<=space.usedBytes||space.totalBytes-space.usedBytes<line.size()+512) {
        ++g_dropped;errorState("TF card full; recording stopped");closeWriter(false);return;
    }
    const size_t written=fwrite(line.data(),1,line.size(),g_writer);
    if(written!=line.size()||ferror(g_writer)) {
        ++g_dropped;errorState("TF write failed; card full, removed or unavailable");closeWriter(false);return;
    }
    ++g_buffered;++g_rowsSinceFlush;g_writerBytes+=line.size();
    updateEntry(space.activeFile,g_writerBytes,true);
    if(record.stopSession)closeWriter();
    else if(startEvent(record)||g_rowsSinceFlush>=16) {if(!flushWriter())closeWriter(false);}
}
void finishFallbackStop() {
    if(!g_writer)return;
    Record stop;bool have=false;
    portENTER_CRITICAL(&g_stateLock);
    for(unsigned i=0;i<kControlSlots;++i)if(g_controls[i].used&&g_controls[i].haveStop&&std::strncmp(g_controls[i].stop.sessionId,g_activeSession,sizeof(g_activeSession))==0) {
        stop=g_controls[i].stop;have=true;break;
    }
    portEXIT_CRITICAL(&g_stateLock);
    if(!have||bounded(stop.sessionId)!=g_activeSession)return;
    // The bounded slot retains the original stop sequence/time/identity even
    // when its queue entry was rejected. Persist the latest loss counter so
    // tail gaps remain explicit after a reboot or subsequent session.
    stop.reading=false;stop.sample.numeric=false;stop.stopSession=true;
    const uint32_t dropped=g_dropped.load();
    snprintf(stop.message,sizeof(stop.message),"Recording stopped; TF rejected/unconfirmed records=%lu",static_cast<unsigned long>(dropped));
    QueuedRecord recovered;recovered.record=stop;recovered.droppedBefore=dropped;writeRecord(recovered);
}
void processRecord(const QueuedRecord &queued) {
    if(startEvent(queued.record)) {
        finishFallbackStop();
        if(!startWriter(queued.record))releaseControl(queued.record.sessionId);
    }
    writeRecord(queued);
}
void worker(void *) {
    QueuedRecord queued;
    for(;;) {
        if((g_formatRequested.load()||g_remountRequested.load())&&lockFs()) {performMaintenance();unlockFs();}
        const uint32_t closeToken=g_closeToken.exchange(0);
        if(closeToken) {
            if(lockFs()) {if(closeToken==g_downloadToken)closeReader();unlockFs();}
            else g_closeToken.store(closeToken);
        }
        if(xQueueReceive(g_queue,&queued,pdMS_TO_TICKS(50))==pdTRUE) {
            // Do not discard an accepted record because a short HTTP metadata
            // operation currently owns the lock. Publishers never wait here.
            while(!lockFs())vTaskDelay(1);
            performMaintenance();processRecord(queued);--g_pending;unlockFs();
        }
        if(lockFs()) {
            if(g_writer&&g_rowsSinceFlush&&(g_rowsSinceFlush>=16||millis()-g_lastFlush>=1000)) {
                if(!flushWriter())closeWriter(false);
            }
            if(g_pending.load()==0&&g_writer) {
                finishFallbackStop();
            }
            unlockFs();
        }
    }
}
String encode(JsonDocument &doc) {String out;serializeJson(doc,out);return out;}
}

bool begin() {
    if(g_started.load())return true;
    g_fsMutex=xSemaphoreCreateMutex();g_queue=xQueueCreate(kQueueDepth,sizeof(QueuedRecord));
    if(!g_fsMutex||!g_queue) {errorState("Unable to allocate TF worker queue/mutex");return false;}
    g_remounting.store(true);g_remountRequested.store(true);
    if(xTaskCreatePinnedToCore(worker,"meter-tf",8192,nullptr,1,&g_task,0)!=pdPASS) {
        g_remounting.store(false);g_remountRequested.store(false);errorState("Unable to start TF worker");return false;
    }
    g_started.store(true);return true;
}
bool append(const Record &record) {
    if(record.sessionId[0]==0)return true;
    // Reserve the in-flight record before publishing stop control. Otherwise
    // an idle worker could close the file between control and queue handoff.
    // The same state lock makes format acceptance exclusive with this handoff.
    bool blocked=false;
    portENTER_CRITICAL(&g_stateLock);
    if(g_formatting.load())blocked=true;
    else {g_inputActive.store(!record.stopSession);++g_pending;}
    portEXIT_CRITICAL(&g_stateLock);
    if(blocked) {++g_dropped;errorState("TF format in progress; tagged record was dropped");return false;}
    const bool start=startEvent(record);
    if(start&&!reserveControl(record)) {--g_pending;++g_dropped;errorState("TF session-control backlog full; start record was dropped");return false;}
    if(record.stopSession)retainStop(record);
    if(!g_started.load()||!g_queue) {
        if(start)releaseControl(record.sessionId);
        --g_pending;++g_dropped;errorState("TF worker unavailable; tagged record was dropped");return false;
    }
    QueuedRecord queued;queued.record=record;queued.droppedBefore=g_dropped.load();
    if(xQueueSend(g_queue,&queued,0)!=pdTRUE) {
        if(start)releaseControl(record.sessionId);
        --g_pending;++g_dropped;errorState("TF queue full; tagged records were dropped");return false;
    }
    return true;
}
String statusJson() {
    const State state=snapshot();JsonDocument doc;
    doc["supported"]=true;doc["mounted"]=state.mounted;doc["recording"]=state.recording;
    doc["remounting"]=g_remounting.load();doc["activeFile"]=state.activeFile;
    doc["formatting"]=state.formatting;doc["formatResult"]=state.formatResult;doc["formatMessage"]=state.formatMessage;
    doc["formatOperationId"]=state.formatOperationId;doc["formatGeneration"]=state.formatGeneration;
    doc["cardBytes"]=state.cardBytes;doc["totalBytes"]=state.totalBytes;doc["usedBytes"]=state.usedBytes;
    doc["writtenRecords"]=g_written.load();doc["droppedRecords"]=g_dropped.load();
    doc["bufferedRecords"]=g_buffered.load();
    doc["queueDepth"]=g_pending.load();doc["queueCapacity"]=kQueueDepth;doc["downloads"]=g_downloads.load();
    doc["error"]=state.error;doc["listTruncated"]=state.listTruncated;return encode(doc);
}
String filesJson() {
    JsonDocument doc;const State state=snapshot();
    doc["supported"]=true;doc["mounted"]=state.mounted;doc["error"]="";
    // A previous write/drop warning does not make this cached list fail.
    // Keep operation errors distinct so readable logs remain downloadable.
    doc["storageError"]=state.error;doc["truncated"]=state.listTruncated;
    JsonArray files=doc["files"].to<JsonArray>();
    if(!g_started.load()||!lockFs()) {doc["error"]="TF storage busy or worker unavailable; retry listing";return encode(doc);}
    if(g_formatting.load()) {doc["error"]="TF format in progress; retry listing after it completes";unlockFs();return encode(doc);}
    // This copies cached metadata only, never scans FAT in the HTTP thread.
    for(unsigned i=0;i<g_fileCount;++i) {
        JsonObject entry=files.add<JsonObject>();entry["name"]=g_files[i].name;entry["bytes"]=g_files[i].bytes;entry["active"]=g_files[i].active;
    }
    unlockFs();return encode(doc);
}
bool requestRemount(String &error) {
    error="";
    if(!g_started.load()) {error="TF worker unavailable";return false;}
    if(!lockFs()) {error="TF storage busy; retry remount";return false;}
    if(g_formatting.load())error="TF format in progress";
    else if(g_remounting.load())error="TF remount already in progress";
    else if(g_inputActive.load()||g_pending.load()||g_writer)error="Stop recording before remounting the TF card";
    else if(g_downloads.load())error="Finish the active TF download before remounting";
    else {g_remounting.store(true);g_remountRequested.store(true);}
    unlockFs();return error.length()==0;
}
bool requestFormat(const String &operationId,String &error) {
    error="";
    if(!meter_log_format::validOperationId(std::string(operationId.c_str(),operationId.length()))) {error="Format operationId must contain 16..64 ASCII letters, digits, _ or -";return false;}
    if(!g_started.load()) {error="TF worker unavailable";return false;}
    int replay=0;
    portENTER_CRITICAL(&g_stateLock);replay=formatReplayLocked(operationId.c_str());portEXIT_CRITICAL(&g_stateLock);
    if(replay) {if(replay<0)error="TF format operation already processed; use a new explicit confirmation";return replay>0;}
    if(!lockFs()) {error="TF storage busy; retry format with the same operationId";return false;}
    enum {Accepted,OldReplay,Formatting,Remounting,Recording,Downloading} result=Accepted;
    portENTER_CRITICAL(&g_stateLock);
    replay=formatReplayLocked(operationId.c_str());
    if(replay<0)result=OldReplay;
    else if(replay>0)result=Accepted;
    else if(g_formatting.load())result=Formatting;
    else if(g_remounting.load())result=Remounting;
    else if(g_inputActive.load()||g_pending.load()||g_buffered.load()||g_writer||g_state.recording)result=Recording;
    else if(g_downloads.load())result=Downloading;
    else {
        textCopy(g_state.formatOperationId,operationId.c_str());textCopy(g_formatHistory[g_formatHistoryNext],operationId.c_str());
        g_formatHistoryNext=(g_formatHistoryNext+1)%8;
        if(++g_state.formatGeneration==0)++g_state.formatGeneration;
        textCopy(g_state.formatResult,"queued");textCopy(g_state.formatMessage,"TF card format queued; all card data will be erased");
        g_state.formatting=true;g_formatting.store(true);g_formatRequested.store(true);
    }
    portEXIT_CRITICAL(&g_stateLock);unlockFs();
    switch(result) {
    case OldReplay:error="TF format operation already processed; use a new explicit confirmation";break;
    case Formatting:error="TF format already in progress";break;
    case Remounting:error="Wait for the TF remount to finish before formatting";break;
    case Recording:error="Stop recording and wait for queued/buffered records before formatting the TF card";break;
    case Downloading:error="Finish the active TF download before formatting";break;
    default:break;
    }
    return result==Accepted;
}
uint32_t openDownload(const String &name,uint64_t &snapshotBytes,String &error) {
    snapshotBytes=0;error="";
    if(!meter_log_format::validLogName(std::string(name.c_str(),name.length()))) {error="Invalid generated TF log name";return 0;}
    if(!g_started.load()||!lockFs()) {error="TF storage busy or unavailable";return 0;}
    if(!snapshot().mounted||g_remounting.load()||g_formatting.load())error="TF card unavailable, remounting or formatting";
    else if(g_downloads.load())error="A TF download is already active";
    else {
        if(g_writer&&!flushWriter()) {closeWriter(false);error="TF sync failed before download; recording stopped";}
        if(error.length()==0) {
            const String path=String(kDirectory)+"/"+name;
            g_download=cardFS.open(path,FILE_READ);
            if(!g_download||g_download.isDirectory()) {if(g_download)g_download.close();error="TF log not found or unreadable";}
            else {
                if(++g_tokenCounter==0)++g_tokenCounter;
                g_downloadToken=g_tokenCounter;g_downloadRemaining=g_download.size();snapshotBytes=g_downloadRemaining;g_downloads.store(1);
            }
        }
    }
    const uint32_t token=error.length()?0:g_downloadToken;unlockFs();return token;
}
size_t readDownload(uint32_t token,uint8_t *target,size_t maxBytes,String &error) {
    error="";
    if(!target||!maxBytes) {error="A download buffer is required";return 0;}
    if(!g_started.load()||!lockFs()) {error="TF storage busy; retry download";return 0;}
    size_t read=0;
    if(!token||token!=g_downloadToken||!g_download)error="TF download handle is closed";
    else if(g_downloadRemaining) {
        const size_t bytes=static_cast<size_t>(std::min<uint64_t>(std::min<size_t>(maxBytes,kChunkBytes),g_downloadRemaining));
        read=g_download.read(target,bytes);
        if(read==0||read>bytes) {read=0;error="TF download read failed; card may have been removed";closeReader();}
        else g_downloadRemaining-=read;
    }
    unlockFs();return read;
}
void closeDownload(uint32_t token) {
    if(!token||!g_started.load())return;
    if(!lockFs()) {g_closeToken.store(token);return;}
    if(token==g_downloadToken)closeReader();
    unlockFs();
}
}
#else
namespace meter_storage {
bool begin() {return true;}
bool append(const Record &) {return false;}
String statusJson() {return "{\"supported\":false,\"mounted\":false,\"recording\":false,\"remounting\":false,\"formatting\":false,\"formatResult\":\"idle\",\"formatMessage\":\"\",\"formatOperationId\":\"\",\"formatGeneration\":0,\"activeFile\":\"\",\"cardBytes\":0,\"totalBytes\":0,\"usedBytes\":0,\"writtenRecords\":0,\"droppedRecords\":0,\"error\":\"TF storage is unavailable on this board; browser recording remains available\"}";}
String filesJson() {return "{\"supported\":false,\"mounted\":false,\"files\":[],\"error\":\"TF storage is unavailable on this board\"}";}
bool requestRemount(String &error) {error="TF storage is unavailable on this board";return false;}
bool requestFormat(const String &,String &error) {error="TF storage is unavailable on this board";return false;}
uint32_t openDownload(const String &,uint64_t &bytes,String &error) {bytes=0;error="TF storage is unavailable on this board";return 0;}
size_t readDownload(uint32_t,uint8_t *,size_t,String &error) {error="TF storage is unavailable on this board";return 0;}
void closeDownload(uint32_t) {}
}
#endif
