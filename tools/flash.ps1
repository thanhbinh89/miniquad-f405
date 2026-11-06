<#
.SYNOPSIS
    Flash MiniQuad-F405 with STM32CubeProgrammer (STM32_Programmer_CLI).

.DESCRIPTION
    -Via swd  : ST-LINK on J2 (SWDIO pin 2, SWCLK pin 3, NRST pin 4, GND pin 7).
                Writes bootloader + application (miniquad-full.hex), keeps the
                configuration sectors unless -EraseConfig.
    -Via uart : STM32 ROM bootloader on J2 (pin 5 TX, pin 6 RX), 115200 8E1.
                Close the BOOT0 bridge JP1 and reset first (or use the web
                configurator: Firmware > Vào ROM bootloader).

    Everyday updates do not need this script: the web configurator flashes
    the application through the MiniQuad bootloader.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File tools\flash.ps1
    powershell -ExecutionPolicy Bypass -File tools\flash.ps1 -Via uart -Port COM5
    powershell -ExecutionPolicy Bypass -File tools\flash.ps1 -Image build\debug\out\miniquad-full.hex -EraseConfig
#>
param(
    [ValidateSet('swd', 'uart')] [string] $Via = 'swd',
    [string] $Port = 'COM3',
    [string] $Image = '',
    [switch] $EraseConfig
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

$cli = Get-Command 'STM32_Programmer_CLI' -ErrorAction SilentlyContinue | Select-Object -First 1 -ExpandProperty Source
if (-not $cli) {
    foreach ($p in @(
            "$env:STM32_PRG_PATH\STM32_Programmer_CLI.exe",
            'C:\ST\STM32CubeCLT*\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe',
            'C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe')) {
        $m = Get-Item $p -ErrorAction SilentlyContinue | Sort-Object FullName -Descending | Select-Object -First 1
        if ($m) { $cli = $m.FullName; break }
    }
}
if (-not $cli) { throw 'STM32_Programmer_CLI not found: install STM32CubeCLT or STM32CubeProgrammer' }

if (-not $Image) { $Image = Join-Path $root 'build\release\out\miniquad-full.hex' }
if (-not (Test-Path $Image)) { throw "image not found: $Image (run tools\build.ps1 first)" }
$Image = (Resolve-Path $Image).Path

if ($Via -eq 'swd') {
    $conn = @('-c', 'port=SWD', 'mode=UR', 'reset=HWrst')
} else {
    $conn = @('-c', "port=$Port", 'br=115200', 'P=EVEN', 'db=8', 'sb=1')
}

# sectors: 0 bootloader, 1-2 configuration, 3.. application. The download
# erases the sectors it writes; the configuration is kept unless asked.
$cliArgs = $conn
if ($EraseConfig) { $cliArgs += @('-e', '[1', '2]') }
$cliArgs += @('-d', $Image, '-v')
if ($Via -eq 'swd') { $cliArgs += '-rst' } else { $cliArgs += @('-g', '0x08000000') }

Write-Host "programmer: $cli"
Write-Host "image     : $Image"
Write-Host "via       : $Via $(if ($Via -eq 'uart') { $Port })"
& $cli @cliArgs
if ($LASTEXITCODE -ne 0) { throw "STM32_Programmer_CLI failed ($LASTEXITCODE)" }
if ($Via -eq 'uart') { Write-Host 'Done. Open JP1 (BOOT0) again before the next reset.' }
