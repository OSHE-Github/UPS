param(
    [switch]$SkipClean
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$makeExe = "C:\ST\STM32CubeIDE_1.19.0\STM32CubeIDE\plugins\com.st.stm32cube.ide.mcu.externaltools.make.win32_2.2.0.202409170845\tools\bin\make.exe"
$toolchainBin = "C:\ST\STM32CubeIDE_1.19.0\STM32CubeIDE\plugins\com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.13.3.rel1.win32_1.0.0.202411081344\tools\bin"
$buildDir = Join-Path $repoRoot "STM32CubeIDE\Debug"

if (-not (Test-Path $makeExe)) {
    throw "make.exe not found at $makeExe"
}

if (-not (Test-Path $toolchainBin)) {
    throw "Toolchain bin directory not found at $toolchainBin"
}

$env:Path = "$toolchainBin;$env:Path"

if (-not $SkipClean) {
    & $makeExe -C $buildDir clean
    if ($LASTEXITCODE -ne 0) {
        throw "Clean build failed with exit code $LASTEXITCODE"
    }
}

& $makeExe -C $buildDir all
if ($LASTEXITCODE -ne 0) {
    throw "Build failed with exit code $LASTEXITCODE"
}
