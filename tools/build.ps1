<#
.SYNOPSIS
    Build the MiniQuad-F405 bootloader + application on Windows with the GNU
    toolchain shipped by STM32CubeCLT / STM32CubeIDE.

.DESCRIPTION
    Finds arm-none-eabi-gcc (PATH, then C:\ST\STM32CubeCLT*, then the
    STM32CubeIDE plug-in), a build tool (ninja, make on PATH, or the make.exe
    plug-in of STM32CubeIDE) and CMake, then configures and builds.
    Images end up in build\<config>\out.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File tools\build.ps1
    powershell -ExecutionPolicy Bypass -File tools\build.ps1 -Config Debug -Clean
#>
param(
    [ValidateSet('Release', 'Debug')] [string] $Config = 'Release',
    [switch] $Clean
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

function Find-First([string[]] $patterns) {
    foreach ($p in $patterns) {
        $m = Get-Item $p -ErrorAction SilentlyContinue | Sort-Object FullName -Descending | Select-Object -First 1
        if ($m) { return $m.FullName }
    }
    return $null
}

function Find-Tool([string] $name, [string[]] $patterns) {
    $cmd = Get-Command $name -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($cmd) { return $cmd.Source }
    return Find-First $patterns
}

$gcc = Find-Tool 'arm-none-eabi-gcc' @(
    'C:\ST\STM32CubeCLT*\GNU-tools-for-STM32\bin\arm-none-eabi-gcc.exe',
    'C:\ST\STM32CubeIDE*\STM32CubeIDE\plugins\*gnu-tools-for-stm32*\tools\bin\arm-none-eabi-gcc.exe')
if (-not $gcc) { throw 'arm-none-eabi-gcc not found: install STM32CubeCLT or add the toolchain to PATH' }
$toolchainDir = Split-Path -Parent $gcc

$cmake = Find-Tool 'cmake' @('C:\Program Files\CMake\bin\cmake.exe', 'C:\ST\STM32CubeCLT*\CMake\bin\cmake.exe')
if (-not $cmake) { throw 'cmake not found' }

$ninja = Find-Tool 'ninja' @('C:\ST\STM32CubeCLT*\Ninja\bin\ninja.exe')
$make = Find-Tool 'make' @('C:\ST\STM32CubeIDE*\STM32CubeIDE\plugins\*externaltools.make*\tools\bin\make.exe')
if ($ninja) { $generator = 'Ninja'; $program = $ninja }
elseif ($make) { $generator = 'MinGW Makefiles'; $program = $make }
else { throw 'neither ninja nor make found' }

$python = Find-Tool 'python' @()
if (-not $python) { $python = Find-Tool 'py' @() }
if (-not $python) { throw 'python 3 not found (needed by tools/fwpack.py)' }

$build = Join-Path $root ('build\' + $Config.ToLower())
if ($Clean -and (Test-Path $build)) { Remove-Item -Recurse -Force $build }

Write-Host "toolchain : $toolchainDir"
Write-Host "generator : $generator ($program)"
& $gcc --version | Select-Object -First 1

& $cmake -S $root -B $build -G $generator `
    "-DCMAKE_MAKE_PROGRAM=$program" `
    "-DCMAKE_TOOLCHAIN_FILE=$root\cmake\arm-none-eabi.cmake" `
    "-DARM_TOOLCHAIN_DIR=$toolchainDir" `
    "-DCMAKE_BUILD_TYPE=$Config" `
    "-DPython3_EXECUTABLE=$python"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& $cmake --build $build --parallel 8
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host ''
Write-Host "Images in $build\out"
Get-ChildItem (Join-Path $build 'out') | Format-Table Name, Length -AutoSize
