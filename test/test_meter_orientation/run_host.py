"""Run portable rotation checks against every canonical board map; no SDK/device."""
import importlib.util
import pathlib
import subprocess
import tempfile

repo = pathlib.Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("board_config", repo / "tools/esp32/meter_board_config.py")
compiler = importlib.util.module_from_spec(spec)
spec.loader.exec_module(compiler)
with tempfile.TemporaryDirectory(prefix="meter_rotation_") as temporary:
    build = pathlib.Path(temporary)
    for name in ("jc3248w535", "lcd147", "amoled175", "lcd349_v1", "lcd349_v2"):
        profile = compiler.load_profile(repo / f"boards/meter/{name}.json")
        header = build / "meter_board_config.h"
        header.write_text(compiler.render_header(profile))
        binary = build / "test.exe"
        subprocess.run(["g++", "-std=c++11", "-Wall", "-Wextra", "-Werror", "-DMETER_ORIENTATION_STANDALONE", "-include", str(header),
                        "-I", str(repo / "lib/meter_ui/src"), "-I", str(repo / "lib/ut_meter"),
                        "-I", str(repo / "lib/jc3248/src"),
                        str(pathlib.Path(__file__).with_name("test_main.cpp")),
                        str(repo / "lib/jc3248/src/jc3248_touch.cpp"), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
        print(f"{name}: all four orientations passed", flush=True)
    for default in range(4):
        subprocess.run(["g++", "-std=c++11", "-Wall", "-Wextra", "-Werror", f"-DMETER_UI_READING_ROTATION={default}", "-DMETER_ORIENTATION_HOST_ADAPTER",
                        "-I", str(repo / "test/test_meter_orientation/fakes"),
                        "-I", str(repo / "lib/meter_ui/src"), "-I", str(repo / "lib/ut_meter"),
                        "-I", str(repo / "src/esp32_touch/meter"),
                        str(pathlib.Path(__file__).with_name("display_cases.cpp")),
                        str(repo / "src/esp32_touch/meter/meter_display.cpp"), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
        print(f"compiled default rotation {default}: persistence passed", flush=True)
