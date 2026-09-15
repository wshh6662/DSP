$ErrorActionPreference = 'Stop'

$project_root = Split-Path -Parent $PSScriptRoot
$output_directory = Join-Path $env:TEMP 'f28384d_pipeline_sorter_tests'
$test_program = Join-Path $output_directory 'test_encoder_watch.exe'

New-Item -ItemType Directory -Force $output_directory | Out-Null

& gcc -std=c99 -Wall -Wextra -Werror `
    -I (Join-Path $PSScriptRoot 'stubs') `
    -I $project_root `
    (Join-Path $PSScriptRoot 'test_encoder_watch.c') `
    (Join-Path $project_root 'encoder_watch.c') `
    -o $test_program

if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

& $test_program
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

Write-Host 'Encoder Watch tests passed.'
