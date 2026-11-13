$ErrorActionPreference = 'Stop'
$repo = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$compilerPath = (Get-Command g++ -ErrorAction Stop).Source
$jsonRoot = Join-Path $repo '.pio/libdeps/ut_meter_jc3248w535/ArduinoJson/src'
if (-not (Test-Path -LiteralPath (Join-Path $jsonRoot 'ArduinoJson.h'))) {
    $candidate = Get-ChildItem -LiteralPath (Join-Path $repo '.pio/libdeps') -Directory |
        ForEach-Object { Join-Path $_.FullName 'ArduinoJson/src' } |
        Where-Object { Test-Path -LiteralPath (Join-Path $_ 'ArduinoJson.h') } |
        Select-Object -First 1
    if (-not $candidate) { throw 'ArduinoJson headers are required from an already prepared meter build. This runner never invokes PlatformIO.' }
    $jsonRoot = $candidate
}
$formatRoot = Join-Path $repo 'lib/meter_log_format/src'
$commonArgs = @('-std=c++11', '-Wall', '-Wextra', '-Werror', '-I', $formatRoot)
$formatExe = Join-Path $env:TEMP "meter_log_format_tests_$PID.exe"
$formatArgs = $commonArgs + @(
    '-DMETER_LOG_FORMAT_STANDALONE',
    (Join-Path $PSScriptRoot 'test_main.cpp'),
    (Join-Path $formatRoot 'meter_log_format.cpp'),
    (Join-Path $formatRoot 'meter_log_io.cpp'), '-o', $formatExe
)
& $compilerPath @formatArgs
if ($LASTEXITCODE -ne 0) { throw 'Portable formatting/real-stdio test compilation failed.' }
& $formatExe
if ($LASTEXITCODE -ne 0) { throw 'Portable formatting/real-stdio tests failed.' }

$workerExe = Join-Path $env:TEMP "meter_storage_host_tests_$PID.exe"
$workerArgs = $commonArgs + @(
    '-pthread', '-DMETER_STORAGE_HOST_TEST', '-DUT_METER_JC3248',
    '-include', (Join-Path $PSScriptRoot 'host/fake_stdio.h'),
    '-I', (Join-Path $PSScriptRoot 'host'), '-I', $jsonRoot,
    '-I', (Join-Path $repo 'src/esp32_touch/meter'),
    (Join-Path $PSScriptRoot 'storage_host_main.cpp'),
    (Join-Path $repo 'src/esp32_touch/meter/meter_storage.cpp'),
    (Join-Path $formatRoot 'meter_log_format.cpp'),
    (Join-Path $formatRoot 'meter_log_io.cpp'), '-o', $workerExe
)
& $compilerPath @workerArgs
if ($LASTEXITCODE -ne 0) { throw 'Worker simulation compilation failed.' }
& $workerExe
if ($LASTEXITCODE -ne 0) { throw 'Worker simulation tests failed.' }
Write-Output 'All portable format/checked-stdio and simulated worker tests passed. No hardware was accessed.'
