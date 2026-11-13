"""Record exact project build inputs after a successful PlatformIO image build."""
from pathlib import Path
import hashlib,json

def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def inputs(root):
    paths=[root/'platformio.ini']
    extensions={'.c','.cpp','.h','.hpp','.inc','.S','.json','.ttf','.html','.css','.js'}
    for base in ('src','lib','boards'):
        paths.extend(p for p in (root/base).rglob('*') if p.is_file() and p.suffix in extensions)
    tools=root/'tools/esp32'
    paths.extend(p for p in tools.glob('*.py') if p.name.startswith('build_meter_') or p.name in {'meter_board_build.py','meter_board_config.py','meter_ui_validation.py','release_provenance.py'})
    return {p.relative_to(root).as_posix():digest(p) for p in sorted(set(paths))}

def register(env):
    root=Path(env.subst('$PROJECT_DIR')).resolve();initial=inputs(root)
    def record(target,source,env):
        current=inputs(root)
        if current!=initial:raise RuntimeError('Build inputs changed during compilation; rerun PlatformIO before releasing')
        app=Path(str(target[0])).resolve();header=app.parent/'meter_board_config.h'
        metadata=dict(environment=env.subst('$PIOENV'),app_sha256=digest(app),board_header_sha256=digest(header),inputs=current)
        (app.parent/'release-inputs.json').write_text(json.dumps(metadata,indent=2),encoding='utf-8')
    env.AddPostAction('$BUILD_DIR/${PROGNAME}.bin',record)
