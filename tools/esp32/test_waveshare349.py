"""Behavior checks for the 3.49 revision mappings and portable board code."""
import copy
import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('board', ROOT/'tools/esp32/meter_board_config.py')
board = importlib.util.module_from_spec(spec)
spec.loader.exec_module(board)


class Waveshare349Tests(unittest.TestCase):
    def profile(self, revision):
        return board.load_profile(ROOT/f'boards/meter/lcd349_{revision}.json')

    def test_revisions_select_distinct_reset_and_backlight(self):
        for rev,reset,bl,exp_reset,exp_bl in [('v1',21,8,-1,-1),('v2',-1,42,5,1)]:
            with self.subTest(rev=rev):
                p=self.profile(rev)
                self.assertEqual((p['display']['width'],p['display']['height']),(172,640))
                self.assertEqual((p['display']['reset'],p['display']['backlight']),(reset,bl))
                self.assertEqual((p['devices']['lcd_reset'],p['devices']['backlight_enable']),(exp_reset,exp_bl))
                self.assertEqual(p['devices']['power_enable'],6)
                self.assertEqual((p['devices']['sda'],p['devices']['scl']),(47,48))
                self.assertEqual((p['devices']['rtc_address'],p['devices']['imu_address']),(0x51,0x6b))
                self.assertEqual(p['storage']['backend'],'sdmmc_1bit')
                self.assertEqual((p['storage']['clk'],p['storage']['cmd'],p['storage']['d0']),(41,39,40))
                h=board.render_header(p)
                self.assertIn('#define UT_METER_LCD349 1',h)
                self.assertIn('#define METER_STORAGE_SDMMC 1',h)
                self.assertNotIn('#define UT_METER_JC3248',h)

    def test_devices_and_card_pin_conflicts_fail(self):
        for section,key,value in [('devices','sda',11),('storage','clk',9),('devices','lcd_reset',6),('devices','rtc_address',0x20)]:
            p=copy.deepcopy(self.profile('v2'));p[section][key]=value
            with self.subTest(section=section,key=key),self.assertRaises(ValueError):board.validate_profile(p)

    def test_portable_rtc_touch_and_motion(self):
        with tempfile.TemporaryDirectory() as temp:
            exe=Path(temp)/'board_test.exe'
            subprocess.run(['g++','-std=c++11','-Wall','-Wextra','-Werror','-I',str(ROOT/'lib/waveshare349/src'),
                            str(ROOT/'test/test_waveshare349/host.cpp'),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)

    def test_real_board_service_with_missing_and_stopped_devices(self):
        with tempfile.TemporaryDirectory() as temp:
            header=board.compile_profile(ROOT/'boards/meter/lcd349_v2.json',temp)
            exe=Path(temp)/'service_test.exe'
            subprocess.run(['g++','-std=c++11','-Wall','-Wextra','-Werror','-include',str(header),
                            '-I',str(ROOT/'test/test_waveshare349/fakes'),'-I',str(ROOT/'lib/waveshare349/src'),
                            '-I',str(ROOT/'src/esp32_touch/meter'),'-I',str(ROOT/'.pio/libdeps/ut_meter_lcd349_v2/ArduinoJson/src'),
                            str(ROOT/'test/test_waveshare349/service_host.cpp'),str(ROOT/'src/esp32_touch/meter/meter_board_devices.cpp'),
                            '-o',str(exe)],check=True)
            for case in ('valid','stopped','12-hour','rtc-write-fail','imu-nack','imu-short','missing-imu','missing-rtc','missing-expander'):
                subprocess.run([str(exe),case],check=True)

    def test_panel_spi_and_pixel_contract(self):
        with tempfile.TemporaryDirectory() as temp:
            header=board.compile_profile(ROOT/'boards/meter/lcd349_v2.json',temp)
            exe=Path(temp)/'panel_test.exe'
            subprocess.run(['g++','-std=c++11','-Wall','-Wextra','-Werror','-include',str(header),
                            '-I',str(ROOT/'test/test_waveshare349/fakes'),'-I',str(ROOT/'lib/waveshare349/src'),
                            '-I',str(ROOT/'src/esp32_touch'),'-I',str(ROOT/'.pio/libdeps/ut_meter_lcd349_v2/ArduinoJson/src'),
                            str(ROOT/'test/test_waveshare349/panel_host.cpp'),str(ROOT/'src/esp32_touch/meter/panel_lcd349.cpp'),
                            '-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)


if __name__=='__main__':unittest.main()
