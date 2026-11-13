# Meter card log boundaries

`meter_log_format` is portable C++11 code for the TF recorder's generated
filenames, bounded format-operation IDs, CSV serialization and checked stdio synchronization. It does not
mount, format, delete or enumerate files.

Only `meter-<16 hexadecimal boot digits>-<nonzero uint32 counter>.csv` is an
accepted log name. Labels and other user text never become paths. Metadata
CSV fields are quoted, embedded quotes are doubled, and spreadsheet formula
prefixes are escaped. Invalid, nonfinite and event values export as blank
numeric cells. Unsynchronized epoch timestamps stay blank; uptime remains
explicit. The additional `droppedBefore` column carries the cumulative count
of rejected or unconfirmed records known before the row. A recovered stop
footer carries the final count when the original stop entry hit a full queue.

`meter_log_io::checkedFlush()` checks `fflush`, `ferror`, and `fsync(fileno)`
on the ESP32. Its Windows host equivalent uses `_commit`. The storage worker
counts `writtenRecords` only after this filesystem sync succeeds and exposes
`bufferedRecords` separately. Filesystem sync cannot guarantee a removable
card's internal power-loss protection.

The storage module's explicit format operation is separate from mounting.
Ordinary boot/remount always passes `format_if_empty=false`. A confirmed
format request requires a 16..64-character ASCII alphanumeric/underscore/hyphen
operation ID. The last eight accepted IDs suppress repeated erase requests;
the current operation retains its queued/running/success/failed result. A
format is refused until recording, queued/buffered rows, downloads and remounts
are idle. The worker formats only a fresh diskio drive returned for the card's
CS10/HSPI registration, verifies its logical-to-physical mapping, then performs
a normal no-format mount. Failed formatting/remounting remains an error.

Run `test/test_meter_log_format/run_host_tests.ps1` from PowerShell. It needs
host `g++` and ArduinoJson headers from an existing prepared meter build; it
does not run PlatformIO. The format suite uses real host stdio and a broken
pipe to reproduce a buffered write that fails at `fflush`. The worker suite
compiles the actual storage module with in-memory card and task fixtures,
injecting queue stalls and checked stdio/sync/close failures. These tests prove
software behavior, not card wiring, card compatibility or live recording.
Format tests erase only the in-memory card fixture and cover exact drive
selection, busy guards, replay IDs, format/remount failure, missing or blank
cards, and recording after successful format. No physical format is run.
