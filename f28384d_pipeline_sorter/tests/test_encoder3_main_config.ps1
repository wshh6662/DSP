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

if ($main_source -notmatch 'turntable_speed_init\(\);') {
    throw 'The turntable speed module must be initialized by main.'
}

if ($main_source -notmatch 'photoelectric_tcp_server_init\(\)') {
    throw 'The photoelectric TCP server must be initialized by main.'
}

if ($main_source -notmatch 'photoelectric_sensor_update\(\);') {
    throw 'The main loop must continuously refresh the photoelectric input.'
}

if ($main_source -notmatch 'turntable_speed_get_rpm_x100\(\);') {
    throw 'The main loop must take one speed snapshot per iteration.'
}

if ($main_source -notmatch 'photoelectric_tcp_server_poll\(\s*g_photoelectric_detected\s*,\s*speed_snapshot\s*\);') {
    throw 'The main loop must publish the detected state and the speed snapshot over TCP.'
}

if ($main_source -notmatch 'DEVICE_DELAY_US\(10000U\);') {
    throw 'The combined Watch and photoelectric loop delay must be 10000 us.'
}

if ($main_source -notmatch '(?m)^\s*EINT;\s*$') {
    throw 'Main must enable global interrupts with EINT before entering the loop.'
}

if ($main_source -notmatch '(?m)^\s*ERTM;\s*$') {
    throw 'Main must clear DBGM with ERTM before entering the loop.'
}

if ($main_source -match 'photoelectric_tcp_server_poll\(g_photoelectric_detected\);') {
    throw 'The TCP poll must also receive the speed snapshot.'
}

foreach ($inactive_poll in @('ModbusTcpServerPoll', 'EncoderTcpServerPoll')) {
    if ($main_source -match "\b$inactive_poll\s*\(") {
        throw "Inactive network service is still called by main: $inactive_poll"
    }
}

Write-Host 'Encoder 3 main-loop configuration test passed.'
