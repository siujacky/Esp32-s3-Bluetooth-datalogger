#pragma once
// Compile-time libc boundary for worker-only host simulations. Portable IO
// tests compile without this header and exercise the real host stdio calls.
#include <cstdio>
#include <io.h>
#include "fake_runtime.h"
inline int hostFlush(FILE *stream){return std::fflush(stream);}
struct FakeWriter {std::shared_ptr<FakeNode> node;std::string buffered;bool error=false;};
inline FILE *fake_fopen(const char *name,const char *) {
    const std::string prefix="/meter-tf",path=name;
    std::lock_guard<std::mutex> guard(fakeCard.mutex);
    if(!fakeCard.present.load()||path.compare(0,prefix.size(),prefix)!=0)return nullptr;
    const std::string relative=path.substr(prefix.size());
    if(fakeCard.nodes.count(relative))return nullptr;
    auto writer=new FakeWriter;writer->node=fakeCard.add(relative);return reinterpret_cast<FILE*>(writer);
}
inline size_t fake_fwrite(const void *data,size_t size,size_t count,FILE *stream) {
    if(fakeCard.pauseWrites.load()) {
        fakeCard.writeBlocked.store(true);
        while(fakeCard.pauseWrites.load())std::this_thread::sleep_for(std::chrono::milliseconds(1));
        fakeCard.writeBlocked.store(false);
    }
    auto writer=reinterpret_cast<FakeWriter*>(stream);
    if(!fakeCard.present.load()||fakeCard.failNextWrite.exchange(false)){writer->error=true;return 0;}
    writer->buffered.append(static_cast<const char*>(data),size*count);return count;
}
inline int fake_fflush(FILE *stream) {
    if(stream==stdout||stream==stderr||!stream)return hostFlush(stream);
    auto writer=reinterpret_cast<FakeWriter*>(stream);
    std::lock_guard<std::mutex> guard(fakeCard.mutex);
    if(!fakeCard.present.load()||fakeCard.failFlush.exchange(false)||writer->error){writer->error=true;return EOF;}
    const uint64_t used=fakeCard.used(),space=fakeCard.capacity>used?fakeCard.capacity-used:0;
    if(writer->buffered.size()>space) {
        writer->node->data.append(writer->buffered.data(),static_cast<size_t>(space));writer->buffered.clear();writer->error=true;return EOF;
    }
    writer->node->data+=writer->buffered;writer->buffered.clear();return 0;
}
inline int fake_ferror(FILE *stream){return reinterpret_cast<FakeWriter*>(stream)->error?1:0;}
inline int fake_fileno(FILE *){return 100;}
inline int fake_fsync(int){return fakeCard.present.load()&&!fakeCard.failSync.exchange(false)?0:-1;}
inline int fake_fclose(FILE *stream) {
    const int flush=fake_fflush(stream);delete reinterpret_cast<FakeWriter*>(stream);
    return fakeCard.failClose.exchange(false)?EOF:flush;
}
#define fopen fake_fopen
#define fwrite fake_fwrite
#define fflush fake_fflush
#define ferror fake_ferror
#define fclose fake_fclose
#define fileno fake_fileno
#define fsync fake_fsync
#define _fileno fake_fileno
#define _commit fake_fsync
