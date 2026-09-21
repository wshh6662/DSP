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

if ($main_source -notmatch 'photoelectric_speed_init\(\);') {
    throw 'The photoelectric block-time speed module must be initialized by main.'
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

if ($main_source -notmatch 'photoelectric_speed_get_rpm_x100\(\);') {
    throw 'The main loop must take one photoelectric speed snapshot per iteration.'
}

if ($main_source -notmatch 'turntable_angle_get_degrees_x100\(\);') {
    throw 'The main loop must take one current-angle snapshot per iteration.'
}

if ($main_source -notmatch 'photoelectric_tcp_server_poll\(\s*g_photoelectric_detected\s*,\s*encoder_speed_snapshot\s*,\s*optical_speed_snapshot\s*,\s*angle_snapshot_x100\s*\)') {
    throw 'The main loop must publish encoder speed, optical speed, object state and angle over TCP.'
}

if ($main_source -notmatch 'turntable_speed_reset_encoder3\(\);') {
    throw 'The re command must reset eQEP3, speed state, angle state and photoelectric speed together.'
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

# ---- Timer0 中断：1 ms 刷光电，10 个 tick 才采一次编码器 ----
$turntable_source = Get-Content -Raw -LiteralPath `
    (Join-Path $project_root 'turntable_speed.c')
$turntable_isr = [regex]::Match(
    $turntable_source,
    'TURNTABLE_SPEED_ISR void turntable_speed_timer_isr\(void\)\s*\{(?<body>.*?)\n\}',
    [System.Text.RegularExpressions.RegexOptions]::Singleline
)

if (-not $turntable_isr.Success) {
    throw 'turntable_speed_timer_isr() was not found in turntable_speed.c.'
}

$isr_body = $turntable_isr.Groups['body'].Value

if (-not $isr_body.Contains('photoelectric_sensor_update();')) {
    throw 'The Timer0 ISR must sample the photoelectric input.'
}

if (-not $isr_body.Contains('photoelectric_speed_update(g_photoelectric_enter_count,')) {
    throw 'The Timer0 ISR must feed the edge counters into the photoelectric speed module.'
}

if (-not $isr_body.Contains('TURNTABLE_ENCODER_SAMPLE_INTERVAL_TICKS')) {
    throw 'The Timer0 ISR must divide the 1 ms tick down to the 10 ms encoder sample.'
}

if (-not $isr_body.Contains('turntable_speed_sample_position(EQEP_getPosition(myEQEP3_BASE));')) {
    throw 'The divided encoder sample must read eQEP3.'
}

foreach ($forbidden in @('send(', 'recv(', 'photoelectric_tcp_build_payload')) {
    if ($isr_body.Contains($forbidden)) {
        throw "The Timer0 ISR must not do TCP work or string building: $forbidden"
    }
}

if (-not $turntable_source.Contains('turntable_angle_update_delta(delta);')) {
    throw 'Every encoder delta must be passed to the current-angle calculator.'
}

if (-not $turntable_source.Contains('photoelectric_speed_reset();')) {
    throw 'The re command must also reset the photoelectric speed state.'
}

# ---- 时基常量必须两边一致 ----
$turntable_header = Get-Content -Raw -LiteralPath `
    (Join-Path $project_root 'turntable_speed.h')

if ($turntable_header -notmatch '#define\s+TURNTABLE_TIMER_TICK_FREQUENCY_HZ\s+1000U') {
    throw 'The Timer0 tick frequency must be 1000 Hz.'
}

if ($turntable_header -notmatch '#define\s+TURNTABLE_ENCODER_SAMPLE_INTERVAL_TICKS\s+10U') {
    throw 'The encoder must still be sampled every 10 ticks (10 ms).'
}

if ($turntable_header -notmatch '#define\s+TURNTABLE_SPEED_SAMPLES_PER_WINDOW\s+5U') {
    throw 'The M-method window must still be 5 samples (50 ms).'
}

$speed_header = Get-Content -Raw -LiteralPath `
    (Join-Path $project_root 'photoelectric_speed.h')

if ($speed_header -notmatch '#define\s+PHOTOELECTRIC_TIMEBASE_TICK_MS\s+1U') {
    throw 'The photoelectric timebase tick must be 1 ms.'
}

if ($main_source -match 'photoelectric_speed_timer_tick') {
    throw 'The removed photoelectric_speed_timer_tick() entry point is still referenced by main.'
}

Write-Host 'Encoder 3 main-loop configuration test passed.'
