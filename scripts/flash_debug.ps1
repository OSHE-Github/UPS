param(
    [string]$Port = "SWD",
    [string]$Mode = "UR"
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$programmerCli = "C:\ST\STM32CubeIDE_1.19.0\STM32CubeIDE\plugins\com.st.stm32cube.ide.mcu.externaltools.cubeprogrammer.win32_2.2.200.202503041107\tools\bin\STM32_Programmer_CLI.exe"
$elfPath = Join-Path $repoRoot "STM32CubeIDE\Debug\ups_bringup.elf"

if (-not (Test-Path $programmerCli)) {
    throw "STM32_Programmer_CLI.exe not found at $programmerCli"
}

if (-not (Test-Path $elfPath)) {
    throw "ELF file not found at $elfPath. Build the firmware first."
}

& $programmerCli -c "port=$Port" "mode=$Mode" -d $elfPath -v
if ($LASTEXITCODE -ne 0) {
    throw "Flash failed with exit code $LASTEXITCODE"
}
