$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$testProgram = Join-Path $env:TEMP 'test_camera_result.exe'

& gcc -std=c99 -Wall -Wextra -Werror `
    -I $projectRoot `
    (Join-Path $PSScriptRoot 'test_camera_result.c') `
    (Join-Path $projectRoot 'camera_result.c') `
    -o $testProgram
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& $testProgram
exit $LASTEXITCODE
