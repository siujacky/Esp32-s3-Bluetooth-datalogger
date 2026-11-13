$ErrorActionPreference='Stop'
$repo=(Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$test=Join-Path $repo 'test/test_meter_value_persistence'
$unity=Join-Path $repo '.pio/libdeps/native/Unity/src'
$exe=Join-Path $env:TEMP ('meter_value_persistence_'+$PID+'.exe')
& g++ -std=c++11 -Wall -Wextra -Werror -I "$repo/lib/meter_ui/src" -I "$repo/lib/ut_meter" -I "$test/fakes" -I "$repo/src/esp32_touch/meter" -I $unity "$test/test_main.cpp" "$repo/src/esp32_touch/meter/meter_value_settings.cpp" "$unity/unity.c" -o $exe
if($LASTEXITCODE -ne 0){exit $LASTEXITCODE}
& $exe
exit $LASTEXITCODE
