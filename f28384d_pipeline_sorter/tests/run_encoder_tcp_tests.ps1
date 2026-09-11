$ErrorActionPreference = 'Stop'

$projectRoot = Split-Path -Parent $PSScriptRoot
$outputDirectory = Join-Path $env:TEMP 'f28384d_pipeline_sorter_tests'
$testExecutable = Join-Path $outputDirectory 'test_encoder_tcp_payload.exe'

New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null

& gcc `
    -std=c99 `
    -Wall `
    -Wextra `
    -Werror `
    -I $projectRoot `
    (Join-Path $PSScriptRoot 'test_encoder_tcp_payload.c') `
    (Join-Path $projectRoot 'encoder_tcp_payload.c') `
    -o $testExecutable

if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

& $testExecutable
exit $LASTEXITCODE
