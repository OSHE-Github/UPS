"""Compile and link the production STM32L152RC bring-up image; never flash."""
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
FW = ROOT / "STM32CubeIDE"
OUT = ROOT / "tests/display_nucleo/build/ups"


def main():
    gcc = shutil.which("arm-none-eabi-gcc")
    if not gcc:
        raise SystemExit("Add Arm GNU Toolchain bin to PATH.")
    OUT.mkdir(parents=True, exist_ok=True)
    flags = ["-mcpu=cortex-m3", "-mthumb", "-Os", "-g3", "-ffunction-sections",
             "-fdata-sections", "-DUSE_HAL_DRIVER", "-DSTM32L152xC"]
    for directory in ["Core/Inc", "Drivers/STM32L1xx_HAL_Driver/Inc",
                      "Drivers/CMSIS/Device/ST/STM32L1xx/Include", "Drivers/CMSIS/Include"]:
        flags += ["-I", str(FW / directory)]
    sources = list((FW / "Core/Src").glob("*.c"))
    hal = FW / "Drivers/STM32L1xx_HAL_Driver/Src"
    sources += [hal / f"stm32l1xx_hal{s}.c" for s in
                ["", "_cortex", "_rcc", "_rcc_ex", "_gpio", "_i2c", "_dma",
                 "_pwr", "_pwr_ex", "_adc", "_adc_ex", "_flash", "_flash_ex"]]
    sources += [FW / "Core/Startup/startup_stm32l152rctx.s"]
    objects = []
    for source in sources:
        obj = OUT / (source.stem + ".o")
        warnings = ["-std=gnu11", "-Wall", "-Wextra", "-Werror"] if source.suffix == ".c" else []
        if source.parent == hal:
            # ST's existing Flash driver has an unused parameter without asserts.
            warnings += ["-Wno-error=unused-parameter"]
        subprocess.run([gcc, *flags, *warnings, "-c", str(source), "-o", str(obj)], check=True)
        objects.append(str(obj))
    elf = OUT / "ups_bringup_adc.elf"
    subprocess.run([gcc, *flags, *objects, "-T", str(FW / "STM32L152RCTX_FLASH.ld"),
                    "--specs=nano.specs", "--specs=nosys.specs", "-Wl,--gc-sections",
                    "-o", str(elf), "-lm"], check=True)
    for fmt, ext in [("ihex", "hex"), ("binary", "bin")]:
        subprocess.run([str(Path(gcc).with_name("arm-none-eabi-objcopy.exe")),
                        "-O", fmt, str(elf), str(elf.with_suffix("." + ext))], check=True)
    subprocess.run([str(Path(gcc).with_name("arm-none-eabi-size.exe")), str(elf)], check=True)
    print("Built production RC bring-up firmware. Not programmed or bench tested.")


if __name__ == "__main__":
    main()
