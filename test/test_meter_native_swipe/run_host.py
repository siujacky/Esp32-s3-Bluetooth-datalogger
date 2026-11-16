"""Exercise native menu geometry on every real board map without an SDK/device."""
import importlib.util
import pathlib
import subprocess
import tempfile

repo = pathlib.Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("board_config", repo / "tools/esp32/meter_board_config.py")
compiler = importlib.util.module_from_spec(spec)
spec.loader.exec_module(compiler)
with tempfile.TemporaryDirectory(prefix="meter_native_swipe_") as temporary:
    build = pathlib.Path(temporary)
    for name in ("jc3248w535", "lcd147", "amoled175", "lcd349_v1", "lcd349_v2"):
        profile = compiler.load_profile(repo / f"boards/meter/{name}.json")
        header = build / "meter_board_config.h"
        header.write_text(compiler.render_header(profile))
        binary = build / "test.exe"
        subprocess.run(["g++", "-std=c++11", "-DMETER_NATIVE_SWIPE_STANDALONE", "-Wall", "-Wextra", "-Werror", "-include", str(header),
                        "-I", str(repo / "lib/meter_ui/src"), "-I", str(repo / "lib/ut_meter"),
                        str(pathlib.Path(__file__).with_name("test_main.cpp")), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
        print(f"{name}: native menu geometry passed in all four rotations", flush=True)
