"""Compile a harness profile and export its HEX; never program a board."""
import argparse
from pathlib import Path
import shutil
import subprocess
import os

HERE = Path(__file__).resolve().parent
PROFILES = {
    "uno-bq": ("arduino:avr:uno", "-DUPS_EMULATOR_PD=0"),
    "uno-pd": ("arduino:avr:uno", "-DUPS_EMULATOR_PD=1"),
    "mega-both": ("arduino:avr:mega:cpu=atmega2560", "-DUPS_EMULATOR_DUAL=1"),
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("profile", choices=PROFILES)
    parser.add_argument("--cli", help="Path to arduino-cli")
    args = parser.parse_args()
    cli = args.cli or shutil.which("arduino-cli")
    if not cli:
        bundled = Path(os.environ.get("LOCALAPPDATA", "")) / (
            "Programs/Arduino IDE/resources/app/lib/backend/resources/arduino-cli.exe")
        if bundled.is_file():
            cli = str(bundled)
    if not cli:
        raise SystemExit("Install Arduino CLI / Arduino IDE or supply --cli.")
    fqbn, flags = PROFILES[args.profile]
    output = HERE / "build" / args.profile
    subprocess.run([cli, "compile", "--fqbn", fqbn, "--warnings", "all",
                    "--build-property", f"compiler.cpp.extra_flags={flags}",
                    "--output-dir", str(output), str(HERE / "power_emulator")], check=True)
    print(f"Built {args.profile}: {output}\nNot programmed or bench tested.")


if __name__ == "__main__":
    main()
