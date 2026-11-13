"""Compile portable overlay checks against the actual canonical JSON UI maps.

Only the host g++ is invoked; this script never builds/flashes firmware.
"""
import json
from pathlib import Path
import subprocess
import tempfile


def flatten(value, path=()):
    if isinstance(value, dict):
        for key, child in value.items():
            yield from flatten(child, path + (key,))
    else:
        yield "METER_UI_" + "_".join(path).upper(), str(value).lower()


def main():
    repo = Path(__file__).resolve().parents[2]
    with tempfile.TemporaryDirectory(prefix="meter_overlay_") as temporary:
        build = Path(temporary)
        for path in sorted((repo / "boards/meter").glob("*.json")):
            profile = json.loads(path.read_text())
            if "extends" in profile:
                continue
            header = build / "meter_board_config.h"
            header.write_text("#define METER_BOARD_CONFIGURED 1\n" + "\n".join(
                f"#define {key} {value}" for key, value in flatten(profile["ui"])) + "\n")
            binary = build / "test.exe"
            subprocess.run(["g++", "-std=c++11", "-Wall", "-Wextra", "-Werror",
                            "-DMETER_OVERLAY_STANDALONE", "-DMETER_OVERLAY_TEST_MAPPED", "-include", str(header),
                            "-I", str(repo / "lib/meter_ui/src"),
                            "-I", str(repo / "lib/ut_meter"),
                            str(Path(__file__).with_name("test_main.cpp")),
                            str(repo / "lib/ut_meter/ut_trend.cpp"),
                            str(repo / "lib/ut_meter/ut_meter.cpp"),
                            "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)
            print(f"{path.stem}: actual JSON overlay geometry passed", flush=True)


if __name__ == "__main__":
    main()
