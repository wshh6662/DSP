$ErrorActionPreference = 'Stop'

$projectRoot = Split-Path -Parent $PSScriptRoot
$sourcePath = Join-Path $projectRoot 'encoder_watch.c'
$source = Get-Content -Raw $sourcePath
$start = $source.IndexOf('void encoder_watch_update_encoder2(void)')
$end = $source.IndexOf('void encoder_watch_update_encoder3(void)', $start)

if (($start -lt 0) -or ($end -lt 0)) {
    throw 'Encoder 2 Watch update function was not found.'
}

$encoder2Function = $source.Substring($start, $end - $start)

$requiredPatterns = @(
    'g_encoder2_position = EQEP_getPosition(myEQEP2_BASE);',
    'GPIO_readPin(myEQEP2_EQEPA_GPIO)',
    'GPIO_readPin(myEQEP2_EQEPB_GPIO)',
    'GPIO_readPin(myEQEP2_EQEPINDEX_GPIO)'
)

foreach ($pattern in $requiredPatterns) {
    if (-not $encoder2Function.Contains($pattern)) {
        throw "Encoder 2 source mapping missing: $pattern"
    }
}

if (($encoder2Function.Contains('myEQEP1_BASE')) -or
    ($encoder2Function.Contains('myEQEP3_BASE'))) {
    throw 'Encoder 2 Watch update function reads a different eQEP module.'
}

Write-Host 'Encoder 2 source mapping test passed.'
