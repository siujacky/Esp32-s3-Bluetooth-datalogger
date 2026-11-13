"""PlatformIO pre-build hook: select a map, validate, and force-include it.

The generated header lives in $BUILD_DIR. Its hash is also a compiler define,
so changing a selected profile or any ancestor changes SCons' object command
signatures even when the compiler's forced-include scanner misses that header.
"""
import hashlib
import importlib.util
import os
from pathlib import Path


def configure_build(env, profile_override=None):
    project = Path(env.subst("$PROJECT_DIR")).resolve()
    module_path = project / "tools" / "esp32" / "meter_board_config.py"
    spec = importlib.util.spec_from_file_location("meter_board_config", module_path)
    compiler = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(compiler)
    override = os.environ.get("METER_BOARD_PROFILE", "") if profile_override is None else profile_override
    # Named environments retain their known hardware even if an earlier custom
    # build left the generic target's selection in the process environment.
    selected = env.GetProjectOption("custom_meter_board_profile", "") or override
    if not selected:
        raise ValueError("ut_meter_mapped requires METER_BOARD_PROFILE=path/to/board.json")
    selected = Path(selected)
    if not selected.is_absolute():
        selected = project / selected
    header = compiler.compile_profile(selected, env.subst("$BUILD_DIR"))
    fingerprint = hashlib.sha256(header.read_bytes()).hexdigest()
    env.Append(CPPPATH=[str(header.parent)],
               CCFLAGS=["-include", str(header)],
               CPPDEFINES=[("METER_BOARD_PROFILE_FINGERPRINT", '\"' + fingerprint + '\"')])
    return header


if "Import" in globals():
    Import("env")
    configure_build(env)
    provenance_path=Path(env.subst('$PROJECT_DIR'))/'tools/esp32/release_provenance.py'
    provenance_spec=importlib.util.spec_from_file_location('meter_release_provenance',provenance_path)
    provenance=importlib.util.module_from_spec(provenance_spec)
    provenance_spec.loader.exec_module(provenance)
    provenance.register(env)
