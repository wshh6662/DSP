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

if ($main_source -notmatch 'turntable_angle_init\(\);') {
    throw 'The turntable angle module must be initialized by main.'
}

if ($main_source -notmatch 'turntable_speed_init\(\);') {
    throw 'The turntable speed module must be initialized by main.'
}

if ($main_source -notmatch 'photoelectric_tcp_server_init\(\)') {
    throw 'The photoelectric TCP server must be initialized by main.'
}

if ($main_source -match 'photoelectric_sensor_update\(\);') {
    throw 'photoelectric_sensor_update() must run inside the Timer0 ISR, not in main.'
}

if ($main_source -notmatch 'turntable_speed_get_rpm_x100\(\);') {
    throw 'The main loop must take one encoder speed snapshot per iteration.'
}

if ($main_source -notmatch 'turntable_angle_get_degrees_x100\(\);') {
    throw 'The main loop must take one current-angle snapshot per iteration.'
}

if ($main_source -notmatch 'photoelectric_tcp_server_poll\(\s*g_photoelectric_detected\s*,\s*encoder_speed_snapshot\s*,\s*angle_snapshot_x100\s*\)') {
    throw 'The main loop must publish encoder speed, object state and current angle over TCP.'
}

if ($main_source -notmatch 'turntable_speed_reset_encoder3\(\);') {
    throw 'The re command must reset eQEP3, speed state and angle state together.'
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

foreach ($inactive_poll in @('ModbusTcpServerPoll', 'EncoderTcpServerPoll')) {
    if ($main_source -match "\b$inactive_poll\s*\(") {
        throw "Inactive network service is still called by main: $inactive_poll"
    }
}

# Timer0 中断必须同时驱动光电边沿检测和当前角度计算。
$turntable_source = [System.IO.File]::ReadAllText("$project_root\turntable_speed.c")
if ($turntable_source -notmatch 'turntable_speed_timer_isr\(void\)') {
    throw 'turntable_speed_timer_isr() was not found in turntable_speed.c.'
}

if ($turntable_source -notmatch '(?s)turntable_speed_timer_isr\(void\).*?photoelectric_sensor_update\(\);') {
    throw 'The Timer0 ISR must sample the photoelectric input.'
}

if ($turntable_source -notmatch 'turntable_angle_update_delta\(delta\);') {
    throw 'Every encoder delta must be passed to the current-angle calculator.'
}

if ($main_source -match 'photoelectric_speed') {
    throw 'The removed photoelectric speed module is still referenced by main.'
}

Write-Host 'Encoder 3 main-loop configuration test passed.'
