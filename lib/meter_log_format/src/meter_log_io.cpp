#include "meter_log_io.h"
#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif
namespace meter_log_io {
FlushResult checkedFlush(FILE *file) {
    if(!file||fflush(file)!=0)return FlushResult::FlushError;
    if(ferror(file))return FlushResult::StreamError;
#if defined(_WIN32)
    const int result=_commit(_fileno(file));
#else
    const int result=fsync(fileno(file));
#endif
    return result==0?FlushResult::Ok:FlushResult::SyncError;
}
}
