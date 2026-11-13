param(
    [string]$BuildDirectory = (Join-Path $env:TEMP 'meter-protocol-tests'),
    [string]$UnityDirectory = ''
)
$ErrorActionPreference = 'Stop'
$projectDirectory = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../..')).Path
if (-not $UnityDirectory) { $UnityDirectory = Join-Path $projectDirectory '.pio/libdeps/native/Unity/src' }
if (-not (Test-Path -LiteralPath (Join-Path $UnityDirectory 'unity.c'))) {
    throw 'Unity source missing. Supply -UnityDirectory pointing to the Unity src directory.'
}
New-Item -ItemType Directory -Force -Path $BuildDirectory | Out-Null
$resolvedBuildDirectory = (Resolve-Path -LiteralPath $BuildDirectory).Path
$executable = Join-Path $resolvedBuildDirectory 'dmm_tests.exe'
$arguments = @('-std=c++11','-Wall','-Wextra','-Werror','-I',(Join-Path $projectDirectory 'lib/dmm_protocols'),'-I',$UnityDirectory,
    (Join-Path $PSScriptRoot 'test_main.cpp'),
    (Join-Path $projectDirectory 'lib/dmm_protocols/dmm_protocols.cpp'),
    (Join-Path $projectDirectory 'lib/dmm_protocols/uni.cpp'),
    (Join-Path $projectDirectory 'lib/dmm_protocols/owon.cpp'),
    (Join-Path $projectDirectory 'lib/dmm_protocols/binary.cpp'),
    (Join-Path $projectDirectory 'lib/dmm_protocols/zotek.cpp'),
    (Join-Path $UnityDirectory 'unity.c'),'-o',$executable)
& g++ @arguments
if ($LASTEXITCODE -ne 0) { throw "Host compilation failed: $LASTEXITCODE" }
& $executable | Tee-Object -FilePath (Join-Path $resolvedBuildDirectory 'test.log')
if ($LASTEXITCODE -ne 0) { throw "Protocol tests failed: $LASTEXITCODE" }
