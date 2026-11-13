"""Reject maps that compile but would make the controls unusable."""
import copy
import importlib.util
from pathlib import Path
import unittest

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('board_config', HERE / 'meter_board_config.py')
config = importlib.util.module_from_spec(spec)
spec.loader.exec_module(config)


class UsableMappingTests(unittest.TestCase):
    def profile(self, name='jc3248w535'):
        return config.load_profile(HERE.parents[1] / 'boards/meter' / (name + '.json'))

    def test_zero_divisors_and_invisible_controls_are_rejected(self):
        for section, field in [('setup.row', 'font_size'), ('setup.row', 'step'),
                               ('setup.row', 'width'), ('setup.row', 'height'),
                               ('advanced.row', 'step'), ('advanced.row', 'title_size'),
                               ('advanced.keyboard', 'minimum_key_width'),
                               ('advanced.keyboard', 'minimum_key_height')]:
            with self.subTest(section=section, field=field):
                p = self.profile()
                item = p['ui']
                for part in section.split('.'):
                    item = item[part]
                item[field] = 0
                with self.assertRaises(ValueError):
                    config.validate_profile(p)

    def test_header_overlay_row_overlap_and_keyboard_overflow_are_rejected(self):
        mutations = [lambda p: p['ui']['setup']['header'].update(y=70),
                     lambda p: p['ui']['setup']['row'].update(step=20),
                     lambda p: p['ui']['advanced']['up'].update(y=80),
                     lambda p: p['ui']['advanced']['row'].update(gap=64),
                     lambda p: p['ui']['advanced']['row'].update(title_size=8),
                     lambda p: p['ui']['advanced']['keyboard'].update(minimum_key_height=120),
                     lambda p: p['ui']['advanced']['row'].update(padding=2048),
                     lambda p: p['ui']['advanced']['back'].update(w=1,h=1),
                     lambda p: p['ui']['setup'].update(header_font_size=8),
                     lambda p: p['ui']['advanced']['keyboard'].update(font_size=8),
                     lambda p: p['ui']['advanced']['row'].update(info_size=8),
                     lambda p: p['ui']['setup'].update(info_height=0),
                     lambda p: p['ui']['reading']['unit'].update(y=2048),
                     lambda p: p['ui']['advanced']['header'].update(size=8)]
        mutations += [lambda p: p['ui']['reading']['value'].update(baseline=1),
                      lambda p: p['ui']['setup']['network']['qr'].update(w=1,h=1)]
        for i, mutate in enumerate(mutations):
            with self.subTest(case=i):
                p = self.profile();mutate(p)
                with self.assertRaises(ValueError):
                    config.validate_profile(p)

    def test_round_controls_outside_circle_are_rejected(self):
        p = self.profile('amoled175')
        p['ui']['advanced']['back'].update(x=0,y=0)
        with self.assertRaisesRegex(ValueError, 'safe|circle'):
            config.validate_profile(p)

    def test_actual_size_is_validated_in_addition_to_reference_size(self):
        p = self.profile()
        p['display'].update(width=128,height=128)
        with self.assertRaises(ValueError):
            config.validate_profile(p)

    def test_existing_maps_and_safe_inherited_resolution_pass(self):
        for name in ('jc3248w535','amoled175','lcd147'):
            config.validate_profile(self.profile(name))
        p = self.profile();p['display'].update(width=320,height=600)
        config.validate_profile(p)

    def test_modern_grid_cards_must_fit_titles_and_details(self):
        for section in ('setup','advanced'):
            p=self.profile();grid=p['ui'][section]['row' if section=='setup' else 'home_grid']
            grid.update(height=44,step=54)
            with self.subTest(section=section),self.assertRaises(ValueError):config.validate_profile(p)


if __name__ == '__main__':
    unittest.main()
