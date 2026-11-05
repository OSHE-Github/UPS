"""Build only the NUCLEO-L152RE display test; never programs a connected board."""
from pathlib import Path
import hashlib
import shutil
import subprocess
import urllib.request
import argparse

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
FW = ROOT / "STM32CubeIDE"
OUT = HERE / "build"
VENDOR = OUT / "vendor"
REV = "1827333a7f7822282a6a46e4169596b5fb58cf61"  # ST CMSIS Device L1 v2.3.2
FILES = {
    "Include/stm32l152xe.h": "072140dba4886168ff52ebcc97f8c81a155f97eadd1c64385b7d81a84f472578",
    "Source/Templates/gcc/startup_stm32l152xe.s": "b6e3ca853e304280603d2aac9eee53cc0b40f1baf6cff6e6164d696f9e0f6351",
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--emulators", action="store_true",
                        help="Include the Mega or two-Uno I2C1 status harness")
    parser.add_argument("--thermistors", action="store_true",
                        help="Read real 10k B3950 NTCs on PC0 and PC1")
    args = parser.parse_args()
    gcc = shutil.which("arm-none-eabi-gcc")
    objcopy = shutil.which("arm-none-eabi-objcopy")
    size = shutil.which("arm-none-eabi-size")
    if not all((gcc, objcopy, size)):
        raise SystemExit("Add the Arm GNU Toolchain bin directory to PATH first.")
    VENDOR.mkdir(parents=True, exist_ok=True)
    for remote, digest in FILES.items():
        dest = VENDOR / Path(remote).name
        if not dest.exists():
            url = f"https://raw.githubusercontent.com/STMicroelectronics/cmsis-device-l1/{REV}/{remote}"
            data = urllib.request.urlopen(url, timeout=30).read()
            if hashlib.sha256(data).hexdigest() != digest:
                raise SystemExit(f"Downloaded file failed checksum: {remote}")
            dest.write_bytes(data)
        if hashlib.sha256(dest.read_bytes()).hexdigest() != digest:
            raise SystemExit(f"Cached file failed checksum: {dest}")

    # Use the project's existing section layout with the RE device's memory map.
    linker = (FW / "STM32L152RCTX_FLASH.ld").read_text()
    linker = linker.replace("STM32L152RCTx", "STM32L152RETx")
    linker = linker.replace("256KBytes FLASH", "512KBytes FLASH").replace("32KBytes RAM", "80KBytes RAM")
    linker = linker.replace("LENGTH = 32K", "LENGTH = 80K").replace("LENGTH = 256K", "LENGTH = 512K")
    script = OUT / "STM32L152RETX_FLASH.ld"
    script.write_text(linker)
    includes = [
        HERE, VENDOR, FW / "Core/Inc",
        FW / "Drivers/STM32L1xx_HAL_Driver/Inc",
        FW / "Drivers/CMSIS/Device/ST/STM32L1xx/Include",
        FW / "Drivers/CMSIS/Include",
    ]
    common = ["-mcpu=cortex-m3", "-mthumb", "-Os", "-g3", "-ffunction-sections",
              "-fdata-sections", "-DUSE_HAL_DRIVER", "-DSTM32L152xE"]
    if args.emulators:
        common.append("-DUPS_POWER_MOCK")
    if args.thermistors:
        common.append("-DUPS_THERMISTORS")
    for path in includes:
        common += ["-I", str(path)]
    hal = FW / "Drivers/STM32L1xx_HAL_Driver/Src"
    sources = [HERE / "main.c", HERE / "oled.c", FW / "Core/Src/system_stm32l1xx.c"]
    if args.emulators:
        sources.append(HERE / "power_mock.c")
    if args.thermistors:
        sources += [HERE / "temperature_view.c", FW / "Core/Src/thermistors.c",
                    hal / "stm32l1xx_hal_adc.c", hal / "stm32l1xx_hal_adc_ex.c"]
    sources += [hal / f"stm32l1xx_hal{suffix}.c" for suffix in
                ["", "_cortex", "_rcc", "_rcc_ex", "_gpio", "_i2c", "_dma", "_pwr", "_pwr_ex"]]
    sources.append(VENDOR / "startup_stm32l152xe.s")
    objects = []
    for src in sources:
        obj = OUT / (src.stem + ".o")
        flags = ["-std=c11", "-Wall", "-Wextra", "-Werror"] if src.suffix == ".c" else []
        subprocess.run([gcc, *common, *flags, "-c", str(src), "-o", str(obj)], check=True)
        objects.append(str(obj))
    name = "nucleo_l152re_harness" if args.emulators else "nucleo_l152re_oled"
    elf = OUT / (name + ("_adc" if args.thermistors else "") + ".elf")
    subprocess.run([gcc, *common, *objects, "-T", str(script), "--specs=nano.specs",
                    "--specs=nosys.specs", "-Wl,--gc-sections", f"-Wl,-Map={OUT / 'display.map'}",
                    "-o", str(elf), "-lm"], check=True)
    subprocess.run([objcopy, "-O", "ihex", str(elf), str(elf.with_suffix(".hex"))], check=True)
    subprocess.run([objcopy, "-O", "binary", str(elf), str(elf.with_suffix(".bin"))], check=True)
    subprocess.run([size, str(elf)], check=True)
    print(f"Built {elf}\nTarget: NUCLEO-L152RE only. Hardware has not been programmed or tested.")


if __name__ == "__main__":
    main()
