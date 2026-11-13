$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$exe = Join-Path $env:TEMP "meter_display_channel_tests_$PID.exe"
& g++ -std=c++11 -Wall -Wextra -Werror -I (Join-Path $repo 'src/esp32_touch/meter') -I (Join-Path $repo 'lib/ut_meter') -I (Join-Path $repo 'lib/dmm_protocols') -I (Join-Path $repo '.pio/libdeps/native/Unity/src') (Join-Path $PSScriptRoot 'test_main.cpp') (Join-Path $repo '.pio/libdeps/native/Unity/src/unity.c') -o $exe
if ($LASTEXITCODE -ne 0) { throw 'Display channel compilation failed' }
& $exe
if ($LASTEXITCODE -ne 0) { throw 'Display channel checks failed' }
