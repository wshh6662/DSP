$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$testProgram = Join-Path $env:TEMP 'test_camera_trigger.exe'

& gcc -std=c99 -Wall -Wextra -Werror -DCAMERA_TRIGGER_HOST_TEST `
    -I $projectRoot `
    (Join-Path $PSScriptRoot 'test_camera_trigger.c') `
    (Join-Path $projectRoot 'camera_trigger.c') `
    -o $testProgram
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& $testProgram
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$gpioTestProgram = Join-Path $env:TEMP 'test_camera_trigger_gpio.exe'
& gcc -std=c99 -Wall -Wextra -Werror `
    -I (Join-Path $PSScriptRoot 'stubs') `
    -I $projectRoot `
    (Join-Path $PSScriptRoot 'test_camera_trigger_gpio.c') `
    (Join-Path $projectRoot 'camera_trigger.c') `
    -o $gpioTestProgram
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& $gpioTestProgram
exit $LASTEXITCODE
