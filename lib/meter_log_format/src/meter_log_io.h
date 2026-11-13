#pragma once
#include <cstdio>
namespace meter_log_io {
enum class FlushResult {Ok,FlushError,StreamError,SyncError};
// Unlike Arduino File::flush(), both stdio and the filesystem sync result are
// checked. Ok means buffered bytes reached the filesystem's sync boundary.
FlushResult checkedFlush(FILE *file);
}
