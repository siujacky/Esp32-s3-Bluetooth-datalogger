"""Run the production TF worker's existing adversarial tests through SDMMC."""
from pathlib import Path
import importlib.util
import subprocess
import tempfile

root=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('board',root/'tools/esp32/meter_board_config.py')
board=importlib.util.module_from_spec(spec);spec.loader.exec_module(board)
with tempfile.TemporaryDirectory(prefix='meter_sdmmc_') as temp:
    header=board.compile_profile(root/'boards/meter/lcd349_v2.json',temp)
    host=root/'test/test_meter_log_format'
    exe=Path(temp)/'storage.exe'
    subprocess.run(['g++','-std=c++11','-Wall','-Wextra','-Werror','-pthread','-DMETER_STORAGE_HOST_TEST',
                    '-include',str(header),'-include',str(host/'host/fake_stdio.h'),
                    '-I',str(host/'host'),'-I',str(root/'.pio/libdeps/ut_meter_lcd349_v2/ArduinoJson/src'),
                    '-I',str(root/'src/esp32_touch/meter'),'-I',str(root/'lib/meter_log_format/src'),
                    str(host/'storage_host_main.cpp'),str(root/'src/esp32_touch/meter/meter_storage.cpp'),
                    str(root/'lib/meter_log_format/src/meter_log_format.cpp'),str(root/'lib/meter_log_format/src/meter_log_io.cpp'),
                    '-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
print('SDMMC one-bit worker, logging/download, format ownership/guard/failure/replay cases passed; simulated I/O only.')
