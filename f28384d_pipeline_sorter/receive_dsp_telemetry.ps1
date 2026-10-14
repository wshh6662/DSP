# Receive ASCII lines: enc_rpm=29.29,angle=90.00,photo=1
# The DSP sends each second and immediately on photo state changes.
# Computer 2 connects as a TCP client to DSP 192.168.1.60:2000.
param(
    [string]$DspIp = '192.168.1.60',
    [int]$Port = 2000
)

$ErrorActionPreference = 'Stop'
$client = New-Object System.Net.Sockets.TcpClient
$reader = $null

try {
    $client.Connect($DspIp, $Port)
    $reader = New-Object System.IO.StreamReader(
        $client.GetStream(), [System.Text.Encoding]::ASCII)
    Write-Host "Connected to ${DspIp}:${Port}. Press Ctrl+C to stop."

    while ($client.Connected) {
        $line = $reader.ReadLine()
        if ($null -eq $line) {
            break
        }
        Write-Host ('{0:HH:mm:ss}  {1}' -f (Get-Date), $line)
    }

    Write-Host 'DSP closed the TCP connection.'
}
catch {
    Write-Error ("TCP receive failed: " + $_.Exception.Message)
    exit 1
}
finally {
    if ($null -ne $reader) {
        $reader.Dispose()
    }
    $client.Dispose()
}
