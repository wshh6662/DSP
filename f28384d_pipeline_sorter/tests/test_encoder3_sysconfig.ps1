$ErrorActionPreference = 'Stop'

$project_root = Split-Path -Parent $PSScriptRoot
$sysconfig_cli = 'D:\ccs 20\ccs\utils\sysconfig_1.28.0\sysconfig_cli.bat'
$c2000ware_product = 'C:\TI\C2000Ware_26_01_00_00\.metadata\sdk.json'
$output_directory = Join-Path `
    ([System.IO.Path]::GetTempPath()) `
    ('f28384d_encoder3_sysconfig_' + [guid]::NewGuid().ToString('N'))

if (-not (Test-Path -LiteralPath $sysconfig_cli)) {
    throw "SysConfig 1.28 CLI not found: $sysconfig_cli"
}

if (-not (Test-Path -LiteralPath $c2000ware_product)) {
    throw "C2000Ware product metadata not found: $c2000ware_product"
}

try {
    New-Item -ItemType Directory -Path $output_directory | Out-Null

    & $sysconfig_cli `
        --script (Join-Path $project_root 'c2000.syscfg') `
        --output $output_directory `
        --product $c2000ware_product `
        --treatWarningsAsErrors

    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }

    $generated_board = Get-Content `
        -LiteralPath (Join-Path $output_directory 'board.c') `
        -Raw
    $encoder3_init = [regex]::Match(
        $generated_board,
        'void myEQEP3_init\(\)\s*\{(?<body>.*?)\n\}',
        [System.Text.RegularExpressions.RegexOptions]::Singleline
    )

    if (-not $encoder3_init.Success) {
        throw 'Generated myEQEP3_init() was not found.'
    }

    $expected = 'EQEP_setPositionCounterConfig(myEQEP3_BASE,EQEP_POSITION_RESET_MAX_POS,3999U);'
    if (-not $encoder3_init.Groups['body'].Value.Contains($expected)) {
        throw "Encoder 3 generated position configuration is incorrect. Expected: $expected"
    }

    Write-Host 'Encoder 3 SysConfig generation test passed.'
}
finally {
    $resolved_temp = [System.IO.Path]::GetFullPath([System.IO.Path]::GetTempPath())
    $resolved_output = [System.IO.Path]::GetFullPath($output_directory)

    if ($resolved_output.StartsWith($resolved_temp) -and
        (Test-Path -LiteralPath $resolved_output)) {
        Remove-Item -LiteralPath $resolved_output -Recurse -Force
    }
}
