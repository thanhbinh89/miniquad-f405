<#
.SYNOPSIS
    Flash MiniQuad-F405 (bootloader + application) with STM32CubeProgrammer
    (STM32_Programmer_CLI, part of STM32CubeCLT).

.DESCRIPTION
    Needed once per board, to put the MiniQuad bootloader on a new MCU, or to
    repair a broken bootloader. Everyday updates go through the web
    configurator (Firmware page), over USB-C on board v2.

    -Via swd  : ST-LINK, connect under reset.
                Board v2: SWD test pads on the bottom side (3 mm pitch,
                  silkscreen RST CLK DIO 3V3 GND):
                  TP1 +3V3 -> VTref, TP2 SWDIO, TP3 SWCLK, TP4 NRST, TP5 GND.
                  Power the board from USB-C (or the battery) while flashing.
                Board v1: J2 pins 2 SWDIO, 3 SWCLK, 4 NRST, 7 GND (1 = 3V3).
    -Via dfu  : board v2, STM32 ROM bootloader in USB DFU mode on the USB-C
                port ("STM32 BOOTLOADER" in the device list). Enter it from
                the web configurator (Firmware > Vào ROM bootloader (DFU)) or
                with JP1 (BOOT0 to 3.3 V) + reset.
    -Via uart : board v1, STM32 ROM bootloader on J2 (pin 5 TX, pin 6 RX),
                115200 8E1. Close JP1 (BOOT0) and reset first.

    Writes miniquad-full.hex (bootloader + application) and keeps the
    configuration sectors 1-2 unless -EraseConfig.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File tools\flash.ps1
    powershell -ExecutionPolicy Bypass -File tools\flash.ps1 -Via dfu
    powershell -ExecutionPolicy Bypass -File tools\flash.ps1 -Board 1 -Via uart -Port COM5
    powershell -ExecutionPolicy Bypass -File tools\flash.ps1 -Image build\debug\out\miniquad-full.hex -EraseConfig
#>
param(
    [ValidateSet('swd', 'dfu', 'uart')] [string] $Via = 'swd',
    [ValidateSet(1, 2)] [int] $Board = 2,
    [string] $Port = 'COM3',
    [string] $Image = '',
    [switch] $EraseConfig
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

if ($Via -eq 'dfu' -and $Board -eq 1) { throw 'board v1 has no USB connector: use -Via swd or -Via uart' }
if ($Via -eq 'uart' -and $Board -eq 2) { throw 'board v2 has no UART connector: use -Via swd or -Via dfu' }

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

if (-not $Image) {
    $out = if ($Board -eq 1) { 'build\release-v1\out' } else { 'build\release\out' }
    $Image = Join-Path $root "$out\miniquad-full.hex"
}
if (-not (Test-Path $Image)) { throw "image not found: $Image (run tools\build.ps1$(if ($Board -eq 1) { ' -Board 1' }) first)" }
$Image = (Resolve-Path $Image).Path

switch ($Via) {
    'swd'  { $conn = @('-c', 'port=SWD', 'mode=UR', 'reset=HWrst') }
    'dfu'  { $conn = @('-c', 'port=USB1') }
    'uart' { $conn = @('-c', "port=$Port", 'br=115200', 'P=EVEN', 'db=8', 'sb=1') }
}

# sectors: 0 bootloader, 1-2 configuration, 3.. application. The download
# erases the sectors it writes; the configuration is kept unless asked.
$cliArgs = $conn
if ($EraseConfig) { $cliArgs += @('-e', '[1', '2]') }
$cliArgs += @('-d', $Image, '-v')
if ($Via -eq 'swd') { $cliArgs += '-rst' } else { $cliArgs += @('-g', '0x08000000') }

Write-Host "programmer: $cli"
Write-Host "image     : $Image"
Write-Host "board     : v$Board, via $Via $(if ($Via -eq 'uart') { $Port })"
& $cli @cliArgs
if ($LASTEXITCODE -ne 0) { throw "STM32_Programmer_CLI failed ($LASTEXITCODE)" }
switch ($Via) {
    'swd'  { if ($Board -eq 2) { Write-Host 'Done. Remove the ST-LINK, plug USB-C and open the web configurator.' } }
    'dfu'  { Write-Host 'Done. If JP1 (BOOT0) is closed, open it again, then reset (S2).' }
    'uart' { Write-Host 'Done. Open JP1 (BOOT0) again before the next reset.' }
}
