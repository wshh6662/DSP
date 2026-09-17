$ErrorActionPreference = 'Stop'

$project_root = Split-Path -Parent $PSScriptRoot
$main_source = Get-Content -Raw -LiteralPath `
    (Join-Path $project_root 'empty_driverlib_main.c')

if ($main_source -notmatch 'encoder_watch_update_encoder3\(\);') {
    throw 'The main loop must refresh encoder 3 Watch variables.'
}

if ($main_source -notmatch 'photoelectric_sensor_init\(\);') {
    throw 'The photoelectric sensor must be initialized by main.'
}

if ($main_source -notmatch 'photoelectric_tcp_server_init\(\)') {
    throw 'The photoelectric TCP server must be initialized by main.'
}

if ($main_source -notmatch 'photoelectric_sensor_update\(\);') {
    throw 'The main loop must continuously refresh the photoelectric input.'
}

if ($main_source -notmatch 'photoelectric_tcp_server_poll\(g_photoelectric_detected\);') {
    throw 'The main loop must publish the detected state over TCP.'
}

if ($main_source -notmatch 'DEVICE_DELAY_US\(10000U\);') {
    throw 'The combined Watch and photoelectric loop delay must be 10000 us.'
}

foreach ($inactive_poll in @('ModbusTcpServerPoll', 'EncoderTcpServerPoll')) {
    if ($main_source -match "\b$inactive_poll\s*\(") {
        throw "Inactive network service is still called by main: $inactive_poll"
    }
}

Write-Host 'Encoder 3 main-loop configuration test passed.'
