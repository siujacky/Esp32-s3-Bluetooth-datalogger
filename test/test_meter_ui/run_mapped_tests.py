"""Compile the actual flat UI macro adapter against every canonical JSON map.

This uses only Python and the existing host g++; it never invokes PlatformIO or
touches hardware. --sections can check a pending profile handoff before merge.
"""
import argparse
import json
import pathlib
import subprocess
import tempfile
import importlib.util


def flatten(value, path=()):
    if isinstance(value, dict):
        for key, child in value.items():
            yield from flatten(child, path + (key,))
    else:
        yield "METER_UI_" + "_".join(path).upper(), str(value).lower()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--sections", type=pathlib.Path)
    args = parser.parse_args()
    repo = pathlib.Path(__file__).resolve().parents[2]
    spec=importlib.util.spec_from_file_location('board_config',repo/'tools/esp32/meter_board_config.py')
    compiler=importlib.util.module_from_spec(spec);spec.loader.exec_module(compiler)
    profiles = json.loads(args.sections.read_text()) if args.sections else {
        path.stem: compiler.load_profile(path)["ui"]
        for path in (repo / "boards/meter").glob("*.json")
    }
    if len(profiles) < 3:
        raise RuntimeError("Expected the three canonical complete UI maps")
    cases = [(name, ui, ui["advanced"]["reference_width"], ui["advanced"]["reference_height"])
             for name, ui in profiles.items()]
    landscape = next(ui for ui in profiles.values() if ui["advanced"]["reference_width"] == 480)
    cases.append(("synthetic_800x480", landscape, 800, 480))
    with tempfile.TemporaryDirectory(prefix="meter_ui_") as temporary:
        build = pathlib.Path(temporary)
        for name, ui, width, height in cases:
            header = build / "meter_board_config.h"
            header.write_text("#define METER_BOARD_CONFIGURED 1\n" + "\n".join(
                f"#define {key} {value}" for key, value in flatten(ui)) + "\n")
            binary = build / "test.exe"
            subprocess.run(["g++", "-std=c++11", "-Wall", "-Wextra", "-Werror",
                            "-DMETER_UI_STANDALONE", "-DMETER_UI_TEST_MAPPED", f"-DMETER_UI_TEST_WIDTH={width}",
                            f"-DMETER_UI_TEST_HEIGHT={height}", "-include", str(header),
                            "-I", str(repo / "lib/meter_ui/src"),
                            "-I", str(repo / "lib/ut_meter"),
                            str(pathlib.Path(__file__).with_name("test_main.cpp")),
                            "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)
            print(f"{name}: actual JSON -> flat macros -> configuredMap geometry passed", flush=True)


if __name__ == "__main__":
    main()
