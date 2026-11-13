param(
    [string]$BuildDirectory = (Join-Path $env:TEMP 'jc3248-calibration-tests'),
    [string]$UnityDirectory = ''
)
$ErrorActionPreference = 'Stop'
$projectDirectory = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../..')).Path
if (-not $UnityDirectory) { $UnityDirectory = Join-Path $projectDirectory '.pio/libdeps/native/Unity/src' }
if (-not (Test-Path -LiteralPath (Join-Path $UnityDirectory 'unity.c'))) {
    throw 'Unity source missing. Supply -UnityDirectory pointing to the Unity src directory.'
}
New-Item -ItemType Directory -Force -Path $BuildDirectory | Out-Null
$executable = Join-Path (Resolve-Path -LiteralPath $BuildDirectory).Path 'jc3248_calibration_tests.exe'
$arguments = @('-std=c++11','-Wall','-Wextra','-Werror','-pedantic',
    '-I',(Join-Path $projectDirectory 'lib/jc3248/src'),'-I',$UnityDirectory,
    (Join-Path $PSScriptRoot 'test_main.cpp'),
    (Join-Path $projectDirectory 'lib/jc3248/src/jc3248_calibration.cpp'),
    (Join-Path $UnityDirectory 'unity.c'),'-o',$executable)
& g++ @arguments
if ($LASTEXITCODE -ne 0) { throw "Host compilation failed: $LASTEXITCODE" }
& $executable | Tee-Object -FilePath (Join-Path $BuildDirectory 'test-calibration.log')
if ($LASTEXITCODE -ne 0) { throw "Calibration tests failed: $LASTEXITCODE" }
