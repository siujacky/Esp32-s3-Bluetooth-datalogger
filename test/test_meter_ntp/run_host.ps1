$ErrorActionPreference='Stop'
$repo=(Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$unity=Join-Path $repo '.pio/libdeps/native/Unity/src'
$output=Join-Path $env:TEMP ('meter_ntp_'+$PID+'.exe')
& g++ -std=c++11 -Wall -Wextra -Werror -pthread -DMETER_NTP_ADAPTER_TEST -I $unity -I "$PSScriptRoot/fakes" -include "$PSScriptRoot/fakes/clock_redirect.h" "$PSScriptRoot/test_main.cpp" "$PSScriptRoot/adapter_cases.cpp" "$repo/src/esp32_touch/meter/meter_ntp.cpp" "$unity/unity.c" -o $output
if($LASTEXITCODE -ne 0){exit $LASTEXITCODE}
& $output
exit $LASTEXITCODE
