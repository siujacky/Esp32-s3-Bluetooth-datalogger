$ErrorActionPreference='Stop'
$repo=(Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$output=Join-Path $env:TEMP ('meter_ntp_json_'+$PID+'.exe')
& g++ -std=c++11 -Wall -Wextra -Werror -DMETER_NTP_JSON_TEST -I "$PSScriptRoot/fakes" -I "$repo/.pio/libdeps/ut_meter_jc3248w535/ArduinoJson/src" "$PSScriptRoot/json_cases.cpp" -o $output
if($LASTEXITCODE -ne 0){exit $LASTEXITCODE}
& $output
exit $LASTEXITCODE
