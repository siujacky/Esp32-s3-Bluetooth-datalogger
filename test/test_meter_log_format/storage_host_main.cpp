#ifdef METER_STORAGE_HOST_TEST
#if METER_STORAGE_SDMMC
#include "SD_MMC.h"
FakeSDMMC SD_MMC;
int FakeSDMMC::clk=-1,FakeSDMMC::cmd=-1,FakeSDMMC::d0=-1,FakeSDMMC::frequency=0;
bool FakeSDMMC::oneBit=false;
#endif
#include "host/fake_runtime.h"
#include "host/ff.h"
#include "meter_storage.h"
#include <cstdio>
#include <cstdlib>
#include <functional>
FakeCard fakeCard;
FakeSD SD;
PARTITION VolToPart[FF_VOLUMES]={{0,0},{1,0}};
thread_local bool pauseOnCriticalExit=false;
std::atomic<bool> criticalExitPaused{false},releaseCriticalExit{false};
std::atomic<bool> pauseQueueReceive{false},queueReceivePaused{false};
int SPIClass::bus=0;int SPIClass::clock=0;int SPIClass::miso=0;int SPIClass::mosi=0;int SPIClass::cs=0;
#define CHECK(condition) do {if(!(condition)){std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#condition);hostFlush(stderr);std::_Exit(1);}}while(0)
using namespace meter_storage;
static JsonDocument status() {JsonDocument doc;CHECK(!deserializeJson(doc,statusJson()));return doc;}
static void waitFor(const std::function<bool()> &condition) {
    const uint32_t start=millis();
    while(!condition()) {CHECK(millis()-start<4000);vTaskDelay(5);}
}
static void drained(){waitFor([]{return status()["queueDepth"].as<unsigned>()==0;});}
static Record record(unsigned session,uint64_t seq,bool start=false,bool stop=false) {
    Record r;r.seq=seq;r.sample.uptimeMs=seq*100;r.reading=!start&&!stop;r.stopSession=stop;
    std::snprintf(r.sessionId,sizeof(r.sessionId),"A1B2C3D4E5F60708-%u",session);
    strlcpy(r.bootId,"A1B2C3D4E5F60708",sizeof(r.bootId));strlcpy(r.deviceId,"983DAEE91834",sizeof(r.deviceId));
    strlcpy(r.label,"bench",sizeof(r.label));r.sample.numeric=true;r.sample.value=1.25;
    if(start||stop){strlcpy(r.event,"recording",sizeof(r.event));strlcpy(r.message,start?"Recording started":"Recording stopped",sizeof(r.message));}
    return r;
}
static std::string path(unsigned session){return "/meter-logs/meter-A1B2C3D4E5F60708-"+std::to_string(session)+".csv";}
static std::string data(unsigned session) {
    std::lock_guard<std::mutex> guard(fakeCard.mutex);const auto file=fakeCard.nodes.find(path(session));return file==fakeCard.nodes.end()?"":file->second->data;
}
static void remount() {
    String error;CHECK(requestRemount(error));CHECK(error.empty());
    waitFor([]{return !status()["remounting"].as<bool>();});
}
static void test_missing_card_drops_without_creating_files() {
    CHECK(begin());waitFor([]{return !status()["remounting"].as<bool>();});
    CHECK(!status()["mounted"].as<bool>());
    CHECK(std::strlen(status()["error"].as<const char*>())>0);
    CHECK(append(record(1,1,true)));CHECK(append(record(1,2)));CHECK(append(record(1,3,false,true)));drained();
    CHECK(status()["droppedRecords"].as<unsigned>()==3);CHECK(data(1).empty());
    std::puts("PASS missing card drops and never creates a file");
}
static void test_remount_and_start_event_boundary() {
    fakeCard.present.store(true);
    {std::lock_guard<std::mutex> guard(fakeCard.mutex);fakeCard.add("/meter-logs","",true);fakeCard.add(path(9),"preserved\r\n");fakeCard.add("/meter-logs/private.txt","untouched");}
    remount();CHECK(status()["mounted"].as<bool>());CHECK(!fakeCard.formatRequested);
#if METER_STORAGE_SDMMC
    CHECK(FakeSDMMC::clk==41&&FakeSDMMC::cmd==39&&FakeSDMMC::d0==40&&FakeSDMMC::oneBit&&FakeSDMMC::frequency==20000);
#else
    CHECK(SPIClass::bus==HSPI&&SPIClass::clock==12&&SPIClass::miso==13&&SPIClass::mosi==11&&SPIClass::cs==10);
#endif
    JsonDocument files;CHECK(!deserializeJson(files,filesJson()));CHECK(files["files"].size()==1);
    CHECK(files["files"][0]["name"].as<std::string>()=="meter-A1B2C3D4E5F60708-9.csv");
    CHECK(append(record(2,4)));CHECK(append(record(2,5,false,true)));drained();CHECK(data(2).empty());
    CHECK(append(record(3,6,true)));CHECK(append(record(3,7)));CHECK(append(record(3,8,false,true)));drained();
    CHECK(!status()["recording"].as<bool>());CHECK(status()["writtenRecords"].as<unsigned>()==3);
    CHECK(data(3).find("Recording started")!=std::string::npos&&data(3).find("Recording stopped")!=std::string::npos);
    CHECK(data(9)=="preserved\r\n");
    CHECK(append(record(9,9,true)));CHECK(append(record(9,10,false,true)));drained();CHECK(data(9)=="preserved\r\n");
    std::puts("PASS no-format remount, generated names, start-only creation and no overwrite");
}
static void test_download_snapshot_survives_writer_appends() {
    CHECK(append(record(4,20,true)));for(unsigned i=0;i<20;++i)CHECK(append(record(4,21+i)));drained();
    String error;CHECK(!requestRemount(error));CHECK(error.find("recording")!=std::string::npos);
    uint64_t size=0;CHECK(openDownload("../private.txt",size,error)==0);CHECK(size==0&&!error.empty());
    const auto token=openDownload("meter-A1B2C3D4E5F60708-4.csv",size,error);CHECK(token&&size>1024&&error.empty());
    const std::string initial=data(4);CHECK(size==initial.size());CHECK(initial.substr(initial.size()-2)=="\r\n");
    for(unsigned i=0;i<5;++i)CHECK(append(record(4,41+i)));
    CHECK(append(record(4,46,false,true)));drained();
    CHECK(data(4).size()>size);CHECK(!requestRemount(error));CHECK(error.find("download")!=std::string::npos);
    uint8_t bytes[4096];std::string downloaded;
    for(;;) {const size_t count=readDownload(token,bytes,sizeof(bytes),error);CHECK(error.empty());CHECK(count<=1024);if(!count)break;downloaded.append(reinterpret_cast<char*>(bytes),count);}
    CHECK(downloaded==initial);closeDownload(token);CHECK(status()["downloads"].as<unsigned>()==0);
    CHECK(readDownload(token,bytes,sizeof(bytes),error)==0&&!error.empty());
    std::puts("PASS bounded download chunks keep initial length while writer continues");
}
static void test_backpressure_and_dropped_stop_close_without_waiting() {
    const auto dropped=status()["droppedRecords"].as<unsigned>();fakeCard.pauseWrites.store(true);
    CHECK(append(record(5,100,true)));waitFor([]{return fakeCard.writeBlocked.load();});
    const uint32_t start=millis();unsigned rejected=0;
    for(unsigned i=0;i<100;++i)if(!append(record(5,101+i)))++rejected;
    if(!append(record(5,201,false,true)))++rejected;
    // Browser/API callers can start and stop another session while the card
    // is still stalled. Its rejected stop must not overwrite A's recovery.
    if(!append(record(15,202,true)))++rejected;
    if(!append(record(15,203,false,true)))++rejected;
    CHECK(millis()-start<250);CHECK(rejected>0);
    CHECK(status()["queueDepth"].as<unsigned>()<=33);CHECK(status()["droppedRecords"].as<unsigned>()>=dropped+rejected);
    fakeCard.pauseWrites.store(false);drained();waitFor([]{return !status()["recording"].as<bool>();});
    CHECK(data(5).find("Recording started")!=std::string::npos);
    CHECK(data(5).find("Recording stopped")!=std::string::npos);
    const std::string terminalLoss=","+std::to_string(status()["droppedRecords"].as<unsigned>())+"\r\n";
    CHECK(data(5).size()>=terminalLoss.size()&&data(5).compare(data(5).size()-terminalLoss.size(),terminalLoss.size(),terminalLoss)==0);
    CHECK(status()["error"].as<std::string>().find("queue full")!=std::string::npos);
    CHECK(data(15).empty());
    const std::string saved=data(5);remount();
    CHECK(append(record(16,204,true)));CHECK(append(record(16,205,false,true)));drained();
    CHECK(data(5)==saved);
    std::puts("PASS blocked-card overflow keeps A's loss footer despite B churn, remount and a new session");
}
static void test_card_full_and_write_failure_never_report_recording_success() {
    {std::lock_guard<std::mutex> guard(fakeCard.mutex);fakeCard.capacity=fakeCard.used()+100;}
    remount();const auto dropped=status()["droppedRecords"].as<unsigned>();
    CHECK(append(record(6,300,true)));CHECK(append(record(6,301,false,true)));drained();
    CHECK(data(6).empty());CHECK(!status()["recording"].as<bool>());
    CHECK(status()["droppedRecords"].as<unsigned>()>=dropped+2);CHECK(status()["error"].as<std::string>().find("full")!=std::string::npos);
    {std::lock_guard<std::mutex> guard(fakeCard.mutex);fakeCard.capacity=16*1024*1024;}
    remount();CHECK(append(record(7,310,true)));drained();CHECK(status()["recording"].as<bool>());
    const auto written=status()["writtenRecords"].as<unsigned>();fakeCard.failNextWrite.store(true);
    CHECK(append(record(7,311)));CHECK(append(record(7,312,false,true)));drained();
    CHECK(!status()["recording"].as<bool>());CHECK(status()["writtenRecords"].as<unsigned>()==written);
    const std::string lastError=status()["error"].as<std::string>();
    if(lastError.find("write failed")==std::string::npos)std::fprintf(stderr,"write failure reported as: %s\n",lastError.c_str());
    CHECK(lastError.find("write failed")!=std::string::npos);
    std::puts("PASS full card and failed writes surface errors without false recording success");
}
static void test_stop_in_flight_cannot_close_before_its_row_is_queued() {
    CHECK(append(record(8,400,true)));drained();CHECK(status()["recording"].as<bool>());
    criticalExitPaused.store(false);releaseCriticalExit.store(false);
    std::thread stopper([]{pauseOnCriticalExit=true;CHECK(append(record(8,401,false,true)));});
    waitFor([]{return criticalExitPaused.load();});
    // Force the worker to observe the stop-control handoff before queue send.
    vTaskDelay(130);CHECK(status()["recording"].as<bool>());
    releaseCriticalExit.store(true);stopper.join();drained();
    CHECK(data(8).find("Recording stopped")!=std::string::npos);
    CHECK(!status()["recording"].as<bool>());
    std::puts("PASS an in-flight stop preserves its CSV row before the writer closes");
}
static void test_queued_remount_precedes_a_new_session() {
    pauseQueueReceive.store(true);waitFor([]{return queueReceivePaused.load();});
    String error;CHECK(requestRemount(error));
    CHECK(status()["remounting"].as<bool>());
    CHECK(append(record(10,500,true)));CHECK(append(record(10,501)));CHECK(append(record(10,502,false,true)));
    pauseQueueReceive.store(false);drained();
    CHECK(data(10).find("Recording stopped")!=std::string::npos);
    CHECK(!status()["remounting"].as<bool>());
    std::puts("PASS an accepted remount completes before a subsequent session starts writing");
}
static void test_checked_flush_and_sync_failures_abort_snapshot_and_count_only_synced_rows() {
    CHECK(append(record(11,600,true)));drained();
    const auto written=status()["writtenRecords"].as<unsigned>();
    CHECK(append(record(11,601)));drained();
    CHECK(status()["bufferedRecords"].as<unsigned>()==1);
    CHECK(status()["writtenRecords"].as<unsigned>()==written);
    fakeCard.failSync.store(true);String error;uint64_t bytes=0;
    CHECK(openDownload("meter-A1B2C3D4E5F60708-11.csv",bytes,error)==0);
    CHECK(bytes==0&&!error.empty());CHECK(!status()["recording"].as<bool>());
    CHECK(status()["writtenRecords"].as<unsigned>()==written);
    CHECK(status()["bufferedRecords"].as<unsigned>()==0);
    CHECK(status()["error"].as<std::string>().find("fsync failed")!=std::string::npos);
    CHECK(append(record(11,602,false,true)));drained();

    CHECK(append(record(12,610,true)));drained();
    const auto beforeFlush=status()["writtenRecords"].as<unsigned>();
    CHECK(append(record(12,611)));drained();fakeCard.failFlush.store(true);
    CHECK(openDownload("meter-A1B2C3D4E5F60708-12.csv",bytes,error)==0);
    CHECK(bytes==0&&!error.empty());CHECK(!status()["recording"].as<bool>());
    CHECK(status()["writtenRecords"].as<unsigned>()==beforeFlush);
    CHECK(status()["error"].as<std::string>().find("flush failed")!=std::string::npos);
    CHECK(append(record(12,612,false,true)));drained();

    CHECK(append(record(13,620,true)));drained();fakeCard.failClose.store(true);
    CHECK(append(record(13,621,false,true)));drained();
    CHECK(!status()["recording"].as<bool>());
    CHECK(status()["error"].as<std::string>().find("close failed")!=std::string::npos);
    std::puts("PASS checked flush/fsync/close failures abort snapshots and unconfirmed rows never count as written");
}
static void test_sticky_warning_does_not_hide_readable_log_files() {
    CHECK(!status()["error"].as<std::string>().empty());
    JsonDocument files;CHECK(!deserializeJson(files,filesJson()));
    CHECK(files["error"].as<std::string>().empty());
    CHECK(!files["storageError"].as<std::string>().empty());
    CHECK(files["files"].size()>0);
    String error;uint64_t bytes=0;
    const auto token=openDownload("meter-A1B2C3D4E5F60708-3.csv",bytes,error);
    CHECK(token&&bytes>0&&error.empty());
    uint8_t buffer[128];CHECK(readDownload(token,buffer,sizeof(buffer),error)>0&&error.empty());
    closeDownload(token);
    std::puts("PASS a sticky storage warning leaves successful file lists and readable downloads available");
}
static void formatDone() {waitFor([]{return !status()["formatting"].as<bool>();});}
static void test_format_idle_guards_and_replay_safe_success() {
    String error;const auto before=fakeCard.formatCalls.load();
    for(const char *id:{"", "short", "format/1234567890"})CHECK(!requestFormat(id,error)&&!error.empty());
    CHECK(append(record(20,700,true)));drained();
    CHECK(!requestFormat("format-busy-recording-01",error)&&!error.empty());
    CHECK(append(record(20,701)));drained();CHECK(status()["bufferedRecords"].as<unsigned>()==1);
    CHECK(!requestFormat("format-busy-buffered-01",error));
    CHECK(append(record(20,702,false,true)));drained();

    pauseQueueReceive.store(true);waitFor([]{return queueReceivePaused.load();});
    CHECK(append(record(21,710,true)));CHECK(append(record(21,711,false,true)));
    CHECK(!requestFormat("format-busy-queued-01",error));pauseQueueReceive.store(false);drained();
    uint64_t bytes=0;const auto token=openDownload("meter-A1B2C3D4E5F60708-3.csv",bytes,error);CHECK(token);
    CHECK(!requestFormat("format-busy-download-01",error)&&error.find("download")!=std::string::npos);closeDownload(token);
    pauseQueueReceive.store(true);waitFor([]{return queueReceivePaused.load();});
    CHECK(requestRemount(error));CHECK(!requestFormat("format-busy-remount-01",error));
    pauseQueueReceive.store(false);waitFor([]{return !status()["remounting"].as<bool>();});
    CHECK(fakeCard.formatCalls.load()==before);

    fakeCard.pauseMkfs.store(true);
    CHECK(requestFormat("format-explicit-success-01",error)&&error.empty());
    CHECK(status()["formatting"].as<bool>());
    const auto generation=status()["formatGeneration"].as<unsigned>();CHECK(generation>0);
    waitFor([]{return fakeCard.mkfsBlocked.load();});
    CHECK(status()["formatResult"].as<std::string>()=="running");
    CHECK(requestFormat("format-explicit-success-01",error));
    CHECK(!requestFormat("format-other-running-01",error));CHECK(!requestRemount(error));
    CHECK(openDownload("meter-A1B2C3D4E5F60708-3.csv",bytes,error)==0);
    CHECK(!append(record(22,720,true)));CHECK(!append(record(22,721,false,true)));
    fakeCard.pauseMkfs.store(false);formatDone();
    CHECK(status()["formatResult"].as<std::string>()=="success");CHECK(status()["mounted"].as<bool>());
    CHECK(status()["formatOperationId"].as<std::string>()=="format-explicit-success-01");
    CHECK(fakeCard.formatCalls.load()==before+1);
    CHECK(fakeCard.lastFormatDrive=="1:");
#if METER_STORAGE_SDMMC
    CHECK(fakeCard.lastDetachedDrive.empty()); // Fresh raw disk has no mounted logical volume.
#else
    CHECK(fakeCard.lastDetachedDrive=="1:");
#endif
    CHECK(fakeCard.lastFormatFlags==(FM_FAT|FM_FAT32));CHECK(!fakeCard.formatRequested);
    CHECK(fakeCard.flashMarker=="internal-flash-preserved");CHECK(fakeCard.registeredDrive==0xff);
    JsonDocument files;CHECK(!deserializeJson(files,filesJson()));CHECK(files["files"].size()==0);
    for(unsigned i=0;i<4;++i)CHECK(requestFormat("format-explicit-success-01",error));
    CHECK(status()["formatGeneration"].as<unsigned>()==generation);CHECK(fakeCard.formatCalls.load()==before+1);
    CHECK(append(record(23,730,true)));CHECK(append(record(23,731,false,true)));drained();
    CHECK(data(23).find("Recording stopped")!=std::string::npos);remount();
    std::puts("PASS explicit format idle guards, exact card drive, cleared list, usable log and replay safety");
}
static void test_format_failure_remount_failure_and_unformatted_card() {
    String error;fakeCard.failMkfs.store(true);const auto before=fakeCard.formatCalls.load();
    CHECK(requestFormat("format-injected-fail-02",error));formatDone();
    CHECK(fakeCard.formatCalls.load()==before+1);CHECK(status()["formatResult"].as<std::string>()=="failed");
    CHECK(!status()["formatMessage"].as<std::string>().empty());CHECK(!status()["error"].as<std::string>().empty());
    CHECK(!data(23).empty());
    CHECK(!requestFormat("format-explicit-success-01",error)&&error.find("processed")!=std::string::npos);
    CHECK(fakeCard.formatCalls.load()==before+1);

    fakeCard.failFormatRemount.store(true);CHECK(requestFormat("format-remount-fail-03",error));formatDone();
    CHECK(status()["formatResult"].as<std::string>()=="failed");CHECK(!status()["mounted"].as<bool>());
    CHECK(status()["formatMessage"].as<std::string>().find("remount")!=std::string::npos);
    const auto count=fakeCard.formatCalls.load();CHECK(requestFormat("format-remount-fail-03",error));
    CHECK(fakeCard.formatCalls.load()==count);remount();

    fakeCard.unformatted.store(true);remount();CHECK(!status()["mounted"].as<bool>());
    CHECK(requestFormat("format-blank-card-ok-04",error));formatDone();
    CHECK(status()["formatResult"].as<std::string>()=="success"&&status()["mounted"].as<bool>());
    VolToPart[1].pd=0;const auto safeBefore=fakeCard.formatCalls.load();
    CHECK(requestFormat("format-drive-guard-05",error));formatDone();
    CHECK(status()["formatResult"].as<std::string>()=="failed");CHECK(fakeCard.formatCalls.load()==safeBefore);
    CHECK(fakeCard.internalVolumeMounted.load());
    CHECK(fakeCard.flashMarker=="internal-flash-preserved");VolToPart[1].pd=1;
    fakeCard.present.store(false);CHECK(requestFormat("format-missing-card-06",error));formatDone();
    CHECK(status()["formatResult"].as<std::string>()=="failed"&&!status()["mounted"].as<bool>());
    CHECK(!status()["formatMessage"].as<std::string>().empty());fakeCard.present.store(true);remount();
    fakeCard.allocatedDrive=0xff;const auto invalidBefore=fakeCard.formatCalls.load();
    CHECK(requestFormat("format-invalid-drive-07",error));formatDone();
    CHECK(status()["formatResult"].as<std::string>()=="failed"&&fakeCard.formatCalls.load()==invalidBefore);
    CHECK(fakeCard.flashMarker=="internal-flash-preserved");fakeCard.allocatedDrive=1;remount();
    std::puts("PASS format/remount failures stay failed; blank cards work; absent/mismatched drives cannot claim success");
}
int main() {
    test_missing_card_drops_without_creating_files();test_remount_and_start_event_boundary();
    test_download_snapshot_survives_writer_appends();test_backpressure_and_dropped_stop_close_without_waiting();
    test_card_full_and_write_failure_never_report_recording_success();
    test_stop_in_flight_cannot_close_before_its_row_is_queued();
    test_queued_remount_precedes_a_new_session();
    test_checked_flush_and_sync_failures_abort_snapshot_and_count_only_synced_rows();
    test_sticky_warning_does_not_hide_readable_log_files();
    test_format_idle_guards_and_replay_safe_success();
    test_format_failure_remount_failure_and_unformatted_card();
    hostFlush(stdout);std::_Exit(0);
}
#endif
