"""Host tests for the real board-profile compiler (no PlatformIO required)."""
import copy
import importlib.util
import hashlib
import json
from pathlib import Path
import tempfile
import unittest


HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
MODULE = HERE / "meter_board_config.py"
if MODULE.exists():
    spec = importlib.util.spec_from_file_location("meter_board_config", MODULE)
    compiler = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(compiler)
else:
    compiler = None


class ProfileCompilerTests(unittest.TestCase):
    def setUp(self):
        if compiler is None and self._testMethodName != "test_compiler_is_available":
            self.skipTest("compiler not implemented yet")
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.directory = Path(self.temp.name)

    def test_compiler_is_available(self):
        self.assertIsNotNone(compiler, "board profile compiler is not implemented")

    def test_large_screen_theme_and_grids_are_declarative(self):
        p=self.canonical()
        self.assertEqual(p['theme']['style'],1)
        self.assertEqual(p['ui']['setup']['row']['columns'],2)
        self.assertEqual(p['ui']['advanced']['home_grid']['columns'],2)
        self.assertEqual(p['ui']['advanced']['keyboard']['minimum_key_height'],44)
        for change in ('bad-color','bad-style','zero-columns','overlapping-cards'):
            bad=copy.deepcopy(p)
            if change=='bad-color':bad['theme']['accent']=65536
            elif change=='bad-style':bad['theme']['style']=2
            elif change=='zero-columns':bad['ui']['setup']['row']['columns']=0
            else:bad['ui']['advanced']['home_grid']['column_gap']=1000
            with self.subTest(change=change),self.assertRaises(ValueError):
                compiler.validate_profile(bad)

    def test_existing_version1_mapping_gets_safe_legacy_defaults(self):
        p=self.canonical('lcd147');p.pop('theme',None)
        p['ui']['setup']['row'].pop('columns',None);p['ui']['setup']['row'].pop('column_gap',None)
        p['ui']['advanced'].pop('home_grid',None)
        compiler.validate_profile(p)
        self.assertEqual(p['theme']['style'],0)
        self.assertEqual(p['ui']['setup']['row']['columns'],1)

    def test_overview_layout_is_mapped_and_old_profiles_receive_defaults(self):
        p=self.canonical();overview=p['ui']['overview']
        self.assertTrue(overview['show_count_button'])
        self.assertEqual(464,overview['values']['w'])
        p['ui'].pop('overview');compiler.validate_profile(p)
        self.assertIn('overview',p['ui'])
        for bad in ('small','outside','bad-color','bad-toggle'):
            item=copy.deepcopy(p)
            if bad=='small':item['ui']['overview']['scope']['h']=1
            elif bad=='outside':item['ui']['overview']['values']['x']=2000
            elif bad=='bad-color':item['ui']['overview']['slot1_color']=65536
            else:item['ui']['overview']['show_count_button']='yes'
            with self.subTest(bad=bad),self.assertRaises(ValueError):compiler.validate_profile(item)

    def canonical(self, name="jc3248w535"):
        return compiler.load_profile(ROOT / "boards" / "meter" / (name + ".json"))

    def save(self, profile, name="board.json"):
        path = self.directory / name
        path.write_text(json.dumps(profile), encoding="utf-8")
        return path

    def test_three_existing_profiles_keep_hardware_and_backend_flags(self):
        for name, size, flag in (("jc3248w535", (320, 480), "UT_METER_JC3248"),
                                 ("amoled175", (466, 466), "UT_METER_AMOLED"),
                                 ("lcd147", (172, 320), None)):
            with self.subTest(name=name):
                profile = self.canonical(name)
                self.assertEqual((profile["display"]["width"], profile["display"]["height"]), size)
                header = compiler.render_header(profile)
                if flag:
                    self.assertIn("#define " + flag + " 1", header)
                else:
                    self.assertNotIn("#define UT_METER_", header)

    def test_inherited_new_board_changes_dimensions_pins_and_nested_leaf(self):
        child = {"extends": "jc3248w535.json", "id": "new-panel",
                 "display": {"width": 300, "height": 600, "backlight": 2},
                 "ui": {"reading": {"reference_width": 600, "reference_height": 320}}}
        profile = compiler.load_profile(self.save(child))
        self.assertEqual(profile["display"]["bus"]["cs"], 45)
        self.assertEqual(profile["display"]["backlight"], 2)
        header = compiler.render_header(profile)
        self.assertIn("#define METER_DISPLAY_WIDTH 300\n", header)
        self.assertIn("#define METER_UI_READING_REFERENCE_WIDTH 600\n", header)
        self.assertNotIn("extends", header)

    def test_unknown_backend_has_no_default_driver(self):
        profile = self.canonical()
        profile["display"]["backend"] = "guess-my-controller"
        with self.assertRaisesRegex(ValueError, "backend"):
            compiler.load_profile(self.save(profile))

    def test_unsupported_pmic_and_lcd_address_window_are_rejected(self):
        profile = self.canonical()
        profile['touch']['required_pmic_address'] = 0x34
        with self.assertRaisesRegex(ValueError, 'PMIC|pmic'):
            compiler.validate_profile(profile)
        for field in ('offset_x','offset_y'):
            profile = self.canonical('lcd147');profile['display'][field] = 200
            with self.assertRaisesRegex(ValueError, 'window|offset'):
                compiler.validate_profile(profile)

    def test_unknown_key_rejected_in_nested_hardware(self):
        profile = self.canonical()
        profile["touch"]["adress"] = 0x3b
        with self.assertRaisesRegex(ValueError, "adress"):
            compiler.load_profile(self.save(profile))

    def test_unknown_top_level_key_rejected(self):
        profile = self.canonical()
        profile["display_driver"] = "anything"
        with self.assertRaisesRegex(ValueError, "display_driver"):
            compiler.load_profile(self.save(profile))

    def test_invalid_dimension_type_rotation_and_address_rejected(self):
        for section, key, value in (("display", "width", 0), ("display", "height", 1025),
                                    ("display", "width", True), ("touch", "address", 0x80)):
            with self.subTest(key=key, value=value):
                profile = self.canonical()
                profile[section][key] = value
                with self.assertRaises(ValueError):
                    compiler.load_profile(self.save(profile))
        profile = self.canonical()
        profile["ui"]["reading"]["rotation"] = 4
        with self.assertRaisesRegex(ValueError, "rotation"):
            compiler.load_profile(self.save(profile))

    def test_driver_limits_and_calibration_minimum_are_rejected(self):
        for width, height in ((127, 480), (320, 127), (361, 480), (320, 641)):
            with self.subTest(size=(width, height)):
                profile = self.canonical()
                profile["display"]["width"], profile["display"]["height"] = width, height
                with self.assertRaises(ValueError):
                    compiler.load_profile(self.save(profile))

    def test_storage_capability_and_traceable_resolved_json_follow_the_map(self):
        profile = self.canonical()
        header = compiler.render_header(profile)
        macros = dict(line[len("#define "):].split(" ", 1) for line in header.splitlines() if line.startswith("#define "))
        self.assertEqual(macros["METER_STORAGE_ENABLED"], "1")
        compact = json.loads(macros["METER_BOARD_MAP_JSON"])
        self.assertEqual(json.loads(compact), profile)
        self.assertEqual(json.loads(macros["METER_MAPPING_SHA256"]), hashlib.sha256(compact.encode()).hexdigest())
        profile["storage"] = self.canonical("lcd147")["storage"]
        disabled = compiler.render_header(profile)
        self.assertIn("#define METER_STORAGE_ENABLED 0", disabled)

    def test_new_storage_mapping_can_use_separate_spi3_for_lcd(self):
        profile = self.canonical("lcd147")
        profile["storage"] = self.canonical()["storage"]
        loaded = compiler.load_profile(self.save(profile))
        self.assertIn("#define METER_STORAGE_ENABLED 1", compiler.render_header(loaded))

    def test_nonexistent_reserved_and_conflicting_gpio_rejected(self):
        for pin in (22, 26, 19, 49, True):
            with self.subTest(pin=pin):
                profile = self.canonical()
                profile["touch"]["sda"] = pin
                with self.assertRaisesRegex(ValueError, "pin|GPIO"):
                    compiler.load_profile(self.save(profile))
        profile = self.canonical()
        profile["touch"]["reset"] = profile["storage"]["sck"]
        with self.assertRaisesRegex(ValueError, "conflict"):
            compiler.load_profile(self.save(profile))

    def test_unconfigured_optional_reset_irq_are_valid(self):
        profile = self.canonical()
        self.assertEqual(profile["touch"]["reset"], -1)
        self.assertEqual(profile["touch"]["irq"], -1)
        self.assertIn("#define METER_TOUCH_RESET -1", compiler.render_header(profile))

    def test_backend_touch_pair_must_be_supported(self):
        profile = self.canonical()
        profile["touch"]["driver"] = "cst92xx"
        with self.assertRaisesRegex(ValueError, "touch"):
            compiler.load_profile(self.save(profile))

    def test_cycles_and_parent_path_escape_are_rejected(self):
        self.save({"extends": "b.json"}, "a.json")
        self.save({"extends": "a.json"}, "b.json")
        with self.assertRaisesRegex(ValueError, "cycle"):
            compiler.load_profile(self.directory / "a.json")
        for parent in ("../outside.json", str(ROOT / "boards/meter/jc3248w535.json")):
            with self.subTest(parent=parent):
                with self.assertRaisesRegex(ValueError, "relative|escape"):
                    compiler.load_profile(self.save({"extends": parent}))

    def test_duplicate_json_keys_rejected(self):
        path = self.directory / "duplicate.json"
        path.write_text('{"id":"one","id":"two"}', encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "duplicate"):
            compiler.load_profile(path)

    def test_inheritance_depth_is_bounded(self):
        for index in range(18):
            self.save({"extends": str(index + 1) + ".json"}, str(index) + ".json")
        with self.assertRaisesRegex(ValueError, "depth"):
            compiler.load_profile(self.directory / "0.json")

    def test_canonical_calibration_namespace_preserved_but_changed_hardware_isolated(self):
        original = self.canonical()
        self.assertEqual(compiler.calibration_namespace(original), "meter-touch")
        changed = copy.deepcopy(original)
        changed["display"]["width"] = 300
        namespace = compiler.calibration_namespace(changed)
        self.assertNotEqual(namespace, "meter-touch")
        self.assertLessEqual(len(namespace), 15)
        changed["id"] = "cosmetic-id"
        changed["ui"]["reading"]["reference_width"] = 800
        self.assertEqual(compiler.calibration_namespace(changed), namespace)
        changed["ui"]["reading"]["rotation"] = 0
        self.assertNotEqual(compiler.calibration_namespace(changed), namespace)

    def test_changed_profile_rewrites_only_its_own_build_header(self):
        source = self.save({"extends": "jc3248w535.json", "id": "custom-one"})
        first, second = self.directory / "env-one", self.directory / "env-two"
        path = compiler.compile_profile(source, first)
        other = compiler.compile_profile(source, second)
        before = other.read_bytes()
        unchanged_mtime = path.stat().st_mtime_ns
        compiler.compile_profile(source, first)
        self.assertEqual(path.stat().st_mtime_ns, unchanged_mtime)
        source.write_text(json.dumps({"extends": "jc3248w535.json", "id": "custom-two",
                                      "display": {"width": 300}}), encoding="utf-8")
        compiler.compile_profile(source, first)
        self.assertIn("#define METER_DISPLAY_WIDTH 300", path.read_text())
        self.assertEqual(other.read_bytes(), before)

    def test_strings_are_c_literals_without_preprocessor_injection(self):
        profile = self.canonical()
        profile["id"] = 'bad\n#define ATTACK 1'
        with self.assertRaisesRegex(ValueError, "id"):
            compiler.load_profile(self.save(profile))

    def test_build_hook_fingerprints_changed_parent_and_keeps_env_output_isolated(self):
        hook_path = HERE / "meter_board_build.py"
        self.assertTrue(hook_path.exists(), "PlatformIO mapping build hook is not implemented")
        spec = importlib.util.spec_from_file_location("meter_board_build", hook_path)
        hook = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(hook)

        class BuildEnvironment:
            def __init__(self, build_dir):
                self.build_dir, self.appended = build_dir, {}
            def subst(self, value):
                return {"$PROJECT_DIR": str(ROOT), "$BUILD_DIR": str(self.build_dir)}[value]
            def GetProjectOption(self, name, default=None):
                return default
            def Append(self, **values):
                self.appended.update(values)

        parent = self.save(self.canonical(), "parent.json")
        source = self.save({"extends": "parent.json", "id": "hook-test"})
        one = BuildEnvironment(self.directory / "env-one")
        hook.configure_build(one, str(source))
        first_flags = one.appended["CPPDEFINES"]
        updated = self.canonical()
        updated["display"]["width"] = 300
        parent.write_text(json.dumps(updated), encoding="utf-8")
        two = BuildEnvironment(self.directory / "env-two")
        hook.configure_build(two, str(source))
        self.assertNotEqual(first_flags, two.appended["CPPDEFINES"])
        self.assertIn("#define METER_DISPLAY_WIDTH 320", (one.build_dir / "meter_board_config.h").read_text())
        self.assertIn("#define METER_DISPLAY_WIDTH 300", (two.build_dir / "meter_board_config.h").read_text())

    def test_generic_build_requires_explicit_profile(self):
        hook_path = HERE / "meter_board_build.py"
        self.assertTrue(hook_path.exists(), "PlatformIO mapping build hook is not implemented")
        spec = importlib.util.spec_from_file_location("meter_board_build", hook_path)
        hook = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(hook)

        class BuildEnvironment:
            def subst(self, value):
                return str(ROOT)
            def GetProjectOption(self, name, default=None):
                return default

        with self.assertRaisesRegex(ValueError, "METER_BOARD_PROFILE"):
            hook.configure_build(BuildEnvironment(), "")

    def test_named_target_keeps_its_canonical_profile_when_generic_override_is_set(self):
        hook_path = HERE / "meter_board_build.py"
        self.assertTrue(hook_path.exists(), "PlatformIO mapping build hook is not implemented")
        spec = importlib.util.spec_from_file_location("meter_board_build", hook_path)
        hook = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(hook)
        build_directory = self.directory / "canonical-env"

        class BuildEnvironment:
            def subst(self, value):
                return {"$PROJECT_DIR": str(ROOT), "$BUILD_DIR": str(build_directory)}[value]
            def GetProjectOption(self, name, default=None):
                return "boards/meter/jc3248w535.json"
            def Append(self, **values):
                pass

        override = self.save({"extends": "amoled175.json", "id": "custom-amoled"})
        header = hook.configure_build(BuildEnvironment(), str(override)).read_text()
        self.assertIn('#define METER_ID "jc3248w535"', header)
        self.assertIn("#define UT_METER_JC3248 1", header)


if __name__ == "__main__":
    unittest.main()
