$ErrorActionPreference = 'Stop'

$project_root = Split-Path -Parent $PSScriptRoot
$main_source = Get-Content -Raw -LiteralPath `
    (Join-Path $project_root 'empty_driverlib_main.c')

if ($main_source -notmatch 'encoder_watch_update_encoder3\(\);') {
    throw 'The main loop must refresh encoder 3 Watch variables.'
}

if ($main_source -notmatch 'DEVICE_DELAY_US\(1000U\);') {
    throw 'The encoder Watch loop delay must remain 1000 us.'
}

foreach ($inactive_poll in @('ModbusTcpServerPoll', 'EncoderTcpServerPoll')) {
    if ($main_source -match "\b$inactive_poll\s*\(") {
        throw "Inactive network service is still called by main: $inactive_poll"
    }
}

Write-Host 'Encoder 3 main-loop configuration test passed.'
