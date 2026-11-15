"""Resolve and validate version-1 meter mappings; emit an isolated C++ header.

This module uses only Python's standard library and is also the host-test API.
Inheritance can read the mapping directory and the explicitly selected file's
directory. Parents must be relative paths without '..', including through links.
"""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re

_validation_spec = importlib.util.spec_from_file_location('meter_ui_validation', Path(__file__).with_name('meter_ui_validation.py'))
_validation_module = importlib.util.module_from_spec(_validation_spec)
_validation_spec.loader.exec_module(_validation_module)


MAPPING_DIR = Path(__file__).resolve().parents[2] / "boards" / "meter"
MAX_INHERITANCE_DEPTH = 16
BACKENDS = {"axs15231b_qspi": ("axs15231b", "UT_METER_JC3248"),
            "axs15231b_349_qspi": ("axs15231b_349", "UT_METER_LCD349"),
            "co5300_qspi": ("cst92xx", "UT_METER_AMOLED"),
            "jd9853_spi": ("axs5106l", None)}
# GPIO22-25 do not exist; GPIO26-37 belong to this N16R8 board's flash/PSRAM;
# GPIO19/20 are the active native USB connection used by all meter targets.
VALID_PINS = (set(range(22)) | set(range(38, 49))) - {19, 20}
SCHEMA = {
    "schema_version": None, "id": None,
    "theme": {name:None for name in ('style','background','surface','raised','text','muted','accent','warning','border','radius')},
    "display": {"backend": None, "width": None, "height": None,
                "bus": {"cs": None, "sck": None, "data0": None, "data1": None,
                        "data2": None, "data3": None, "dc": None},
                "reset": None, "backlight": None, "frequency": None,
                "offset_x": None, "offset_y": None},
    "touch": {"driver": None, "sda": None, "scl": None, "address": None,
              "reset": None, "irq": None, "frequency": None,
              "mirror_x": None, "mirror_y": None, "swap_xy": None,
              "required_pmic_address": None},
    "storage": {"backend": None, "spi_host": None, "cs": None, "mosi": None,
                "sck": None, "miso": None, "frequency": None,
                "clk": None, "cmd": None, "d0": None},
    "devices": {key:None for key in ('sda','scl','frequency','expander_address','rtc_address','imu_address','lcd_reset','backlight_enable','power_enable')},
    "ui": {name: {"rotation": None, "reference_width": None,
                  "reference_height": None} for name in ("reading", "setup", "advanced")},
}


def _shape(*names):
    return {name: None for name in names}


_RECT = _shape("x", "y", "w", "h")
_LABEL = _shape("x", "y", "size", "align")
_VIEW = _shape("rotation", "reference_width", "reference_height", "safe_circle", "safe_margin")
SCHEMA["ui"] = {
    "reading": {**_VIEW, "value": _shape("baseline", "max_width", "size_x", "size_y"),
                **{name: _LABEL for name in ("profile", "status", "flags", "unit", "slot")},
                "scope": {"plot": _RECT, **{name: _LABEL for name in ("value", "status", "mode", "slot")}}},
    "setup": {**_VIEW, "viewport": _RECT, "header": _RECT,
              "row": _shape("x", "width", "height", "step", "font_size", "columns", "column_gap"),
              "info_height": None, "header_font_size": None, "footer": _LABEL,
              "info": {name: _LABEL for name in ("state", "title", "address", "model", "rssi", "message", "message_second")},
              "network": {"qr": _RECT, **{name: _LABEL for name in ("heading", "ssid", "password_caption", "password", "ip_caption", "ip", "lan")}}},
    "advanced": {**_VIEW, **{name: _RECT for name in ("viewport", "back", "up", "down")},
                 "header": _LABEL, "message": _LABEL,
                 "row": _shape("step", "gap", "title_size", "detail_size", "info_size", "padding", "title_y", "detail_y"),
                 "keyboard": _shape("preferred_columns", "minimum_key_width", "minimum_key_height", "gap", "caption_height", "text_height", "control_height", "footer_height", "font_size"),
                 "home_grid": _shape("columns", "column_gap", "step", "height")},
    "overview": {**{name:_RECT for name in ('values','scope','count_button')},
                 **_shape('show_count_button','gap','slot0_color','slot1_color','slot2_color','slot3_color')},
}
DEFAULT_THEME=dict(style=0,background=0x0841,surface=0x2127,raised=0x2127,text=0xffff,muted=0xa554,accent=0x5f2c,warning=0xfd20,border=0x2127,radius=8)


def _keys(value, schema, path="profile", required=True):
    if not isinstance(value, dict):
        raise ValueError(path + " must be an object")
    unknown = set(value) - set(schema)
    if unknown:
        raise ValueError(path + ": unknown key " + sorted(unknown)[0])
    if required:
        missing = set(schema) - set(value)
        if missing:
            raise ValueError(path + ": missing key " + sorted(missing)[0])
    for key, child in value.items():
        if isinstance(schema[key], dict):
            _keys(child, schema[key], path + "." + key, required)


def _integer(value, minimum, maximum, path):
    if type(value) is not int or not minimum <= value <= maximum:
        raise ValueError(f"{path} must be an integer in {minimum}..{maximum}")


def _boolean(value, path):
    if type(value) is not bool:
        raise ValueError(path + " must be a boolean")


def _pin(value, path, optional=False):
    if type(value) is not int or (value not in VALID_PINS and not (optional and value == -1)):
        raise ValueError(path + " must be an available ESP32-S3 GPIO pin" + (" or -1" if optional else ""))


def default_overview(ui,style=0):
    reading=ui['reading'];rw,rh=reading['reference_width'],reading['reference_height']
    colors=dict(slot0_color=0x3ed8,slot1_color=0x85bf,slot2_color=0xccff,slot3_color=0xfde0)
    if reading.get('safe_circle'):
        area=dict(x=80,y=104,w=306,h=278) if (rw,rh)==(466,466) else dict(x=rw//5,y=rh//4,w=rw*3//5,h=rh//2)
        return dict(values=dict(area),scope=dict(area),count_button=dict(x=(rw-104)//2,y=rh-60,w=104,h=44),show_count_button=False,gap=8,**colors)
    if (rw,rh)==(320,172):return dict(values=dict(x=6,y=32,w=308,h=96),scope=dict(x=6,y=32,w=308,h=96),count_button=dict(x=244,y=130,w=70,h=40),show_count_button=False,gap=4,**colors)
    margin=max(4,rw//40);top=max(24,rh*5//32);reserve=max(28,rh*13//80)
    area=dict(x=margin,y=top,w=rw-2*margin,h=rh-top-reserve)
    return dict(values=dict(area),scope=dict(area),count_button=dict(x=rw-margin-96,y=rh-46,w=96,h=44),show_count_button=style==1 and rw>=400 and rh>=250,gap=8,**colors)

def validate_ui(ui, display, style=0):
    """UI validation interface; shared layout owns the complete version-1 tree."""
    _keys(ui, SCHEMA["ui"], "ui")
    for name in ('reading','setup','advanced'):
        view=ui[name]
        _integer(view["rotation"], 0, 3, "ui." + name + ".rotation")
        _integer(view["reference_width"], 64, 2048, "ui." + name + ".reference_width")
        _integer(view["reference_height"], 64, 2048, "ui." + name + ".reference_height")
        _boolean(view["safe_circle"], "ui." + name + ".safe_circle")
        _integer(view["safe_margin"], 0, min(view["reference_width"], view["reference_height"]) // 4,
                 "ui." + name + ".safe_margin")

        def geometry(value, path):
            for key, item in value.items():
                child = path + "." + key
                if isinstance(item, dict):
                    geometry(item, child)
                elif key == "safe_circle":
                    _boolean(item, child)
                elif key == "align":
                    _integer(item, -1, 1, child)
                else:
                    _integer(item, 0, 2048, child)
            if set(value) == set(_RECT):
                _integer(value["w"], 1, view["reference_width"], path + ".w")
                _integer(value["h"], 1, view["reference_height"], path + ".h")
                if value["x"] + value["w"] > view["reference_width"] or value["y"] + value["h"] > view["reference_height"]:
                    raise ValueError(path + " must be inside its reference canvas")
            if set(value) == set(_LABEL):
                _integer(value["size"], 0, 8, path + ".size")
        geometry(view, "ui." + name)
    overview=ui['overview'];_boolean(overview['show_count_button'],'ui.overview.show_count_button');_integer(overview['gap'],0,32,'ui.overview.gap')
    for key in ('slot0_color','slot1_color','slot2_color','slot3_color'):_integer(overview[key],0,65535,'ui.overview.'+key)
    for key in ('values','scope','count_button'):
        for axis in ('x','y'):_integer(overview[key][axis],0,2048,'ui.overview.'+key+'.'+axis)
        for axis in ('w','h'):_integer(overview[key][axis],1,2048,'ui.overview.'+key+'.'+axis)
    _validation_module.validate_usable_ui(ui, display, style)


def validate_profile(profile):
    # Version-1 profiles from before the visual refresh remain valid. Defaults
    # are explicit in the resolved export/hash and never select a new style.
    if isinstance(profile,dict):
        profile.setdefault('devices',dict(sda=-1,scl=-1,frequency=400000,expander_address=-1,rtc_address=-1,imu_address=-1,lcd_reset=-1,backlight_enable=-1,power_enable=-1))
        if isinstance(profile.get('storage'),dict):
            for key in ('clk','cmd','d0'):profile['storage'].setdefault(key,-1)
        profile.setdefault('theme',dict(DEFAULT_THEME))
        ui=profile.get('ui')
        if isinstance(ui,dict):
            setup=ui.get('setup');advanced=ui.get('advanced')
            if isinstance(setup,dict) and isinstance(setup.get('row'),dict):
                setup['row'].setdefault('columns',1);setup['row'].setdefault('column_gap',0)
            if isinstance(advanced,dict) and isinstance(advanced.get('row'),dict):
                row=advanced['row']
                if isinstance(row.get('step'),int) and isinstance(row.get('gap'),int):advanced.setdefault('home_grid',dict(columns=1,column_gap=0,step=row['step'],height=row['step']-row['gap']))
            reading=ui.get('reading')
            if isinstance(reading,dict) and all(type(reading.get(key)) is int and reading[key]>0 for key in ('reference_width','reference_height')):
                ui.setdefault('overview',default_overview(ui,profile['theme'].get('style',0) if isinstance(profile['theme'],dict) else 0))
                if isinstance(ui.get('overview'),dict):ui['overview'].setdefault('slot3_color',0xfde0)
    _keys(profile, SCHEMA)
    theme=profile['theme'];_integer(theme['style'],0,1,'theme.style');_integer(theme['radius'],0,24,'theme.radius')
    for key in ('background','surface','raised','text','muted','accent','warning','border'):_integer(theme[key],0,65535,'theme.'+key)
    if type(profile["schema_version"]) is not int or profile["schema_version"] != 1:
        raise ValueError("schema_version must be 1")
    if not isinstance(profile["id"], str) or not re.fullmatch(r"[a-z0-9][a-z0-9_-]{0,62}", profile["id"]):
        raise ValueError("id must be a lowercase identifier of 1..63 characters")
    display, touch, storage = profile["display"], profile["touch"], profile["storage"]
    backend = display["backend"]
    if not isinstance(backend, str) or backend not in BACKENDS:
        raise ValueError("unsupported display.backend")
    if touch["driver"] != BACKENDS[backend][0]:
        raise ValueError("unsupported touch.driver for display.backend")
    for name in ("width", "height"):
        _integer(display[name], 128 if backend == "axs15231b_qspi" else 64, 1024, "display." + name)
    if display["width"] * display["height"] * 2 > 2 * 1024 * 1024:
        raise ValueError("display framebuffer exceeds 2 MiB")
    for name in ("offset_x", "offset_y"):
        _integer(display[name], 0, 255, "display." + name)
    # Bound each supported controller's window as well as framebuffer memory.
    # These driver limits remain build proof; a custom panel still needs testing.
    if backend == "co5300_qspi" and (display["width"] + display["offset_x"] > 480 or
                                     display["height"] + display["offset_y"] > 480):
        raise ValueError("CO5300 dimensions and offsets exceed 480x480 GRAM")
    if backend == "axs15231b_qspi" and (display["width"] + display["offset_x"] > 360 or
                                         display["height"] + display["offset_y"] > 640):
        raise ValueError("AXS15231B dimensions and offsets exceed 360x640 driver range")
    if backend == 'axs15231b_349_qspi' and (display['width'],display['height'],display['offset_x'],display['offset_y']) != (172,640,0,0):
        raise ValueError('Waveshare 3.49 backend requires native 172x640 with zero offsets')
    if backend == "jd9853_spi" and (display["width"] > 172 or display["height"] > 320):
        raise ValueError("JD9853 backend currently supports only the established <=172x320 window")
    if backend == "jd9853_spi" and (display["offset_x"] + display["width"] > 206 or display["offset_y"] + display["height"] > 320):
        raise ValueError("JD9853 offsets exceed the established address window")
    _integer(display["frequency"], 1000000, 80000000, "display.frequency")
    pins = {}

    def pin(value, path, optional=False):
        _pin(value, path, optional)
        if value == -1:
            return
        if value in pins:
            raise ValueError(f"GPIO conflict: {path} and {pins[value]} use pin {value}")
        pins[value] = path

    for name, value in display["bus"].items():
        optional = name in (("data1", "data2", "data3") if backend == "jd9853_spi" else ("dc",))
        pin(value, "display.bus." + name, optional)
        if optional and value != -1:
            raise ValueError("display.bus." + name + " is unused by " + backend + "; use -1")
    pin(display["reset"], "display.reset", True)
    pin(display["backlight"], "display.backlight", True)
    if backend == "co5300_qspi" and display["backlight"] != -1:
        raise ValueError("CO5300 uses internal brightness; display.backlight must be -1")
    for name in ("sda", "scl", "reset", "irq"):
        pin(touch[name], "touch." + name, name in ("reset", "irq"))
    _integer(touch["address"], 0x08, 0x77, "touch.address")
    _integer(touch["frequency"], 10000, 400000, "touch.frequency")
    _integer(touch["required_pmic_address"], -1, 0x77, "touch.required_pmic_address")
    if touch["required_pmic_address"] != -1 and touch["required_pmic_address"] < 0x08:
        raise ValueError("touch.required_pmic_address must be -1 or a usable I2C address")
    if touch["required_pmic_address"] != -1 and backend != "co5300_qspi":
        raise ValueError("required PMIC detection is only supported by the CO5300 backend")
    if touch["required_pmic_address"] == touch["address"]:
        raise ValueError("PMIC and touch must have different I2C addresses")
    for name in ("mirror_x", "mirror_y", "swap_xy"):
        _boolean(touch[name], "touch." + name)
    if touch["driver"] in ("axs15231b", "axs15231b_349") and (touch["mirror_x"] or touch["mirror_y"] or touch["swap_xy"]):
        raise ValueError("AXS15231B touch uses its rotation/calibration transform; mirror/swap must be false")
    if touch["driver"] == "cst92xx" and touch["irq"] == -1:
        raise ValueError("CST92xx touch requires a configured irq")
    if storage["backend"] not in ("none", "sd_spi", "sdmmc_1bit"):
        raise ValueError("unsupported storage.backend")
    _integer(storage["spi_host"], 3, 3, "storage.spi_host")
    _integer(storage["frequency"], 1000000, 40000000, "storage.frequency")
    for name in ("cs", "mosi", "sck", "miso"):
        pin(storage[name], "storage." + name, storage["backend"] != "sd_spi")
        if storage["backend"] != "sd_spi" and storage[name] != -1:
            raise ValueError("disabled storage pins must be -1")
    for name in ('clk','cmd','d0'):
        pin(storage[name],'storage.'+name,storage['backend']!='sdmmc_1bit')
        if storage['backend']!='sdmmc_1bit' and storage[name]!=-1:raise ValueError('unused SDMMC pins must be -1')
    devices=profile['devices'];enabled=backend=='axs15231b_349_qspi'
    _integer(devices['frequency'],10000,400000,'devices.frequency')
    for name in ('sda','scl'):
        pin(devices[name],'devices.'+name,not enabled)
        if not enabled and devices[name]!=-1:raise ValueError('devices bus requires Waveshare 3.49 backend')
    addresses=[]
    for name in ('expander_address','rtc_address','imu_address'):
        _integer(devices[name],0x08 if enabled else -1,0x77 if enabled else -1,'devices.'+name)
        if enabled:
            if devices[name] in addresses:raise ValueError('devices I2C address conflict')
            addresses.append(devices[name])
    channels=[]
    for name in ('lcd_reset','backlight_enable','power_enable'):
        _integer(devices[name],-1,7 if enabled else -1,'devices.'+name)
        if devices[name]>=0:
            if devices[name] in channels:raise ValueError('devices expander pin conflict')
            channels.append(devices[name])
    if enabled and devices['power_enable']<0:raise ValueError('3.49 requires expander power-enable pin')
    if enabled and ((display['reset']>=0)==(devices['lcd_reset']>=0)):raise ValueError('3.49 needs exactly one GPIO or expander LCD reset')
    validate_ui(profile["ui"], display, theme['style'])
    return profile


def _object(pairs):
    value = {}
    for key, item in pairs:
        if key in value:
            raise ValueError("duplicate JSON key " + key)
        value[key] = item
    return value


def _inside(path, roots):
    return any(path.is_relative_to(root) for root in roots)


def _merge(base, overlay):
    result = dict(base)
    for key, value in overlay.items():
        result[key] = _merge(result[key], value) if isinstance(value, dict) and isinstance(result.get(key), dict) else value
    return result


def resolve_profile(path, mapping_dir=None):
    """Return (validated mapping, all contributing absolute JSON paths)."""
    mapping_dir = Path(mapping_dir or MAPPING_DIR).resolve()
    selected = Path(path).resolve()
    roots = (mapping_dir, selected.parent)
    dependencies = []

    def read(current, stack):
        if len(stack) >= MAX_INHERITANCE_DEPTH:
            raise ValueError("profile inheritance depth exceeds 16")
        if current in stack:
            raise ValueError("profile inheritance cycle")
        if not _inside(current, roots):
            raise ValueError("profile inheritance path escape")
        try:
            data = json.loads(current.read_text(encoding="utf-8"), object_pairs_hook=_object)
        except (OSError, json.JSONDecodeError) as error:
            raise ValueError(f"cannot read profile {current}: {error}") from error
        if not isinstance(data, dict):
            raise ValueError("profile must be a JSON object")
        parent = data.pop("extends", None)
        _keys(data, SCHEMA, required=False)
        dependencies.append(current)
        if parent is not None:
            if not isinstance(parent, str) or not parent or Path(parent).is_absolute() or ".." in Path(parent).parts or "\\" in parent:
                raise ValueError("extends must be a relative JSON path without path escapes")
            candidates = ((current.parent / parent).resolve(), (mapping_dir / parent).resolve())
            for candidate in candidates:
                if not _inside(candidate, roots):
                    raise ValueError("profile inheritance path escape")
            target = next((candidate for candidate in candidates if candidate.is_file()), candidates[0])
            data = _merge(read(target, stack + (current,)), data)
        return data

    return validate_profile(read(selected, ())), dependencies


def load_profile(path, mapping_dir=None):
    return resolve_profile(path, mapping_dir)[0]


def calibration_namespace(profile):
    display, touch = profile["display"], profile["touch"]
    physical = {"display": display, "touch": touch,
                "rotation": profile["ui"]["reading"]["rotation"]}
    # Preserve existing JC NVS data only for its exact original physical map.
    original_bus = {"cs": 45, "sck": 47, "data0": 21, "data1": 48,
                    "data2": 40, "data3": 39, "dc": -1}
    original_touch = {"driver": "axs15231b", "sda": 4, "scl": 8, "address": 0x3b,
                      "reset": -1, "irq": -1, "frequency": 400000,
                      "mirror_x": False, "mirror_y": False, "swap_xy": False,
                      "required_pmic_address": -1}
    if (display == {"backend": "axs15231b_qspi", "width": 320, "height": 480,
                    "bus": original_bus, "reset": -1, "backlight": 1,
                    "frequency": 40000000, "offset_x": 0, "offset_y": 0}
            and touch == original_touch and physical["rotation"] == 1):
        return "meter-touch"
    digest = hashlib.sha256(json.dumps(physical, sort_keys=True, separators=(",", ":")).encode()).hexdigest()
    return "mt-" + digest[:12]  # Preferences namespaces have a 15-character limit.


def render_header(profile):
    validate_profile(profile)
    lines = ["// Generated by meter_board_config.py; edit the selected JSON profile.",
             "#pragma once", "#define METER_BOARD_CONFIGURED 1"]
    flag = BACKENDS[profile["display"]["backend"]][1]
    if flag:
        lines.append("#define " + flag + " 1")

    def leaves(value, path):
        for key, item in sorted(value.items()):
            child = path + (key.upper(),)
            if isinstance(item, dict):
                leaves(item, child)
            else:
                literal = str(int(item)) if isinstance(item, bool) else json.dumps(item, ensure_ascii=True)
                lines.append("#define METER_" + "_".join(child) + " " + literal)

    leaves(profile, ())
    compact = json.dumps(profile, sort_keys=True, separators=(",", ":"), ensure_ascii=True)
    digest = hashlib.sha256(compact.encode("utf-8")).hexdigest()
    lines.append("#define METER_STORAGE_ENABLED " + str(int(profile["storage"]["backend"] != "none")))
    lines.append("#define METER_STORAGE_SDMMC " + str(int(profile["storage"]["backend"] == "sdmmc_1bit")))
    lines.append("#define METER_MAPPING_SHA256 " + json.dumps(digest))
    lines.append("#define METER_BOARD_MAP_JSON " + json.dumps(compact))
    lines.append('#define METER_TOUCH_CALIBRATION_NAMESPACE ' + json.dumps(calibration_namespace(profile)))
    return "\n".join(lines) + "\n"


def compile_profile(path, output_dir, mapping_dir=None):
    profile = load_profile(path, mapping_dir)
    destination = Path(output_dir) / "meter_board_config.h"
    content = render_header(profile)
    destination.parent.mkdir(parents=True, exist_ok=True)
    if not destination.exists() or destination.read_text(encoding="utf-8") != content:
        temporary = destination.with_suffix(".h.tmp")
        temporary.write_text(content, encoding="utf-8", newline="\n")
        os.replace(temporary, destination)
    return destination


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("profile", type=Path)
    parser.add_argument("output_dir", type=Path)
    args = parser.parse_args()
    try:
        print(compile_profile(args.profile, args.output_dir))
    except ValueError as error:
        parser.error(str(error))
