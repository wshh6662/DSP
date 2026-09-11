$ErrorActionPreference = 'Stop'

$projectRoot = Split-Path -Parent $PSScriptRoot
$mainSource = Get-Content -Raw -LiteralPath (Join-Path $projectRoot 'empty_driverlib_main.c')

if ($mainSource -notmatch '#define\s+MAIN_LOOP_PERIOD_US\s+10000U') {
    throw 'MAIN_LOOP_PERIOD_US must be 10000U for responsive Modbus TCP service.'
}

if ($mainSource -notmatch '#define\s+NETWORK_RETRY_TICKS\s+100U') {
    throw 'NETWORK_RETRY_TICKS must remain equivalent to one second.'
}

if ($mainSource -notmatch '#define\s+MODBUS_TCP_SERVER_PORT\s+502U') {
    throw 'The HMI Modbus TCP server must listen on port 502.'
}

if ($mainSource -notmatch '#define\s+MODBUS_DETECTION_COUNT_VALUE\s+1234U') {
    throw 'The fixed HMI detection-count test value must be 1234.'
}

if ($mainSource -notmatch 'ModbusTcpServerPoll\(\);') {
    throw 'The main loop must service Modbus TCP requests.'
}

foreach ($name in @(
    'g_modbus_last_frame_length',
    'g_modbus_last_mbap_length',
    'g_modbus_last_request_address',
    'g_modbus_last_request_quantity',
    'g_modbus_last_response_length',
    'g_w5500_network_init_result',
    'g_w5500_version',
    'g_w5500_phy_link',
    'g_network_ready_state')) {
    if ($mainSource -notmatch "\b$name\b") {
        throw "Missing Modbus diagnostic variable: $name"
    }
}

Write-Output 'Modbus TCP main-loop configuration test passed.'
