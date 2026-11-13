$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$exe = Join-Path $env:TEMP "meter_ui_tests_$PID.exe"
& g++ -std=c++11 -Wall -Wextra -Werror -DMETER_UI_STANDALONE -I (Join-Path $repo 'lib/meter_ui/src') (Join-Path $PSScriptRoot 'test_main.cpp') -o $exe
if ($LASTEXITCODE -ne 0) { throw 'Portable meter UI compilation failed' }
& $exe
if ($LASTEXITCODE -ne 0) { throw 'Portable meter UI tests failed' }
