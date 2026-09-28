$ErrorActionPreference = 'Stop'

$projectRoot = Split-Path -Parent $PSScriptRoot
$mainPath = Join-Path $projectRoot 'empty_driverlib_main.c'
$tcpHeaderPath = Join-Path $projectRoot 'photoelectric_tcp_server.h'
$cprojectPath = Join-Path $projectRoot '.cproject'

$main = Get-Content -LiteralPath $mainPath -Raw
$tcpHeader = Get-Content -LiteralPath $tcpHeaderPath -Raw
$cproject = Get-Content -LiteralPath $cprojectPath -Raw

$checks = @(
    @{ Name = 'main includes Modbus storage'; Pattern = '#include\s+"modbus_tcp_server\.h"'; Text = $main },
    @{ Name = 'main includes RS485 port'; Pattern = '#include\s+"rs485_modbus_port\.h"'; Text = $main },
    @{ Name = 'main resets Modbus storage'; Pattern = 'ModbusTcp_ResetStorage\s*\(\s*\)'; Text = $main },
    @{ Name = 'main initializes RS485 port'; Pattern = 'rs485_modbus_port_init\s*\(\s*\)'; Text = $main },
    @{ Name = 'main polls RS485 with detection count'; Pattern = 'rs485_modbus_port_poll\s*\(\s*g_photoelectric_enter_count\s*\)'; Text = $main },
    @{ Name = 'main consumes HMI count reset'; Pattern = 'rs485_modbus_port_take_count_reset_request\s*\(\s*\)'; Text = $main },
    @{ Name = 'main requests photoelectric count reset'; Pattern = 'photoelectric_sensor_request_count_reset\s*\(\s*\)'; Text = $main },
    @{ Name = 'main loop uses 1 ms delay'; Pattern = 'DEVICE_DELAY_US\s*\(\s*1000U\s*\)'; Text = $main },
    @{ Name = 'TCP send period remains about 1 second'; Pattern = 'PHOTOELECTRIC_TCP_SEND_PERIOD_TICKS\s+1000U'; Text = $tcpHeader }
)

foreach ($check in $checks) {
    if ($check.Text -notmatch $check.Pattern) {
        throw "Missing integration requirement: $($check.Name)"
    }
}

if (($cproject | Select-String -Pattern 'excluding="[^"]*modbus_tcp_server\.c' -AllMatches).Matches.Count -ne 0) {
    throw 'modbus_tcp_server.c is still excluded from a CCS build configuration'
}

Write-Output 'RS485 main integration checks passed'
