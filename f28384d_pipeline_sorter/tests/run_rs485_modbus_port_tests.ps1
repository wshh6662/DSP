$ErrorActionPreference = 'Stop'

$projectRoot = Split-Path -Parent $PSScriptRoot
$outputDirectory = Join-Path $env:TEMP 'f28384d_pipeline_sorter_tests'
$testExecutable = Join-Path $outputDirectory 'test_rs485_modbus_port.exe'

New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null

& gcc `
    -std=c99 `
    -Wall `
    -Wextra `
    -Werror `
    -I (Join-Path $PSScriptRoot 'stubs') `
    -I $projectRoot `
    (Join-Path $PSScriptRoot 'test_rs485_modbus_port.c') `
    (Join-Path $projectRoot 'rs485_modbus_port.c') `
    (Join-Path $projectRoot 'modbus_rtu_server.c') `
    (Join-Path $projectRoot 'modbus_tcp_server.c') `
    -o $testExecutable

if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

& $testExecutable
exit $LASTEXITCODE
