$ErrorActionPreference = 'Stop'

$project_root = Split-Path -Parent $PSScriptRoot
$output_directory = Join-Path $env:TEMP 'f28384d_pipeline_sorter_tests'
$test_program = Join-Path $output_directory 'test_photoelectric_speed.exe'

New-Item -ItemType Directory -Force $output_directory | Out-Null

& gcc -std=c99 -Wall -Wextra -Werror `
    -I (Join-Path $PSScriptRoot 'stubs') `
    -I $project_root `
    (Join-Path $PSScriptRoot 'test_photoelectric_speed.c') `
    (Join-Path $project_root 'photoelectric_speed.c') `
    -o $test_program

if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

& $test_program
exit $LASTEXITCODE
