$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$exe = Join-Path $env:TEMP "meter_overlay_tests_$PID.exe"
& g++ -std=c++11 -Wall -Wextra -Werror -DMETER_OVERLAY_STANDALONE -I (Join-Path $repo 'lib/meter_ui/src') -I (Join-Path $repo 'lib/ut_meter') (Join-Path $PSScriptRoot 'test_main.cpp') (Join-Path $repo 'lib/ut_meter/ut_trend.cpp') (Join-Path $repo 'lib/ut_meter/ut_meter.cpp') -o $exe
if ($LASTEXITCODE -ne 0) { throw 'Portable meter overlay compilation failed' }
& $exe
if ($LASTEXITCODE -ne 0) { throw 'Portable meter overlay tests failed' }
