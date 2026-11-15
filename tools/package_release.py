"""Package clean, committed builds; never touches connected hardware."""
from pathlib import Path
import argparse,hashlib,importlib.util,json,os,shutil,subprocess,sys,zipfile

ROOT=Path(__file__).resolve().parents[1]
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def run(*args):return subprocess.check_output(args,cwd=ROOT,text=True).strip()
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--version',default='v0.1.0')
    parser.add_argument('--boards',nargs='+',choices=('jc3248w535','amoled175','lcd147','lcd349_v1','lcd349_v2'),
                        default=['jc3248w535','amoled175','lcd147','lcd349_v1','lcd349_v2'])
    parser.add_argument('--esptool',type=Path,default=Path(os.environ.get('PLATFORMIO_CORE_DIR',Path.home()/'.platformio'))/'packages/tool-esptoolpy/esptool.py')
    args=parser.parse_args();version=args.version
    if not version.startswith('v') or any(c not in 'v0123456789.-' for c in version):raise SystemExit('Use a version such as v0.1.0')
    if run('git','status','--porcelain'):raise SystemExit('Commit the release source before packaging')
    commit=run('git','rev-parse','HEAD');out=ROOT/'dist'/version;out.mkdir(parents=True,exist_ok=True)
    framework=args.esptool.parent.parent/'framework-arduinoespressif32'
    boot_app0=framework/'tools/partitions/boot_app0.bin'
    assert args.esptool.is_file() and boot_app0.is_file(),'Build the board targets with PlatformIO first'
    spec=importlib.util.spec_from_file_location('release_provenance',ROOT/'tools/esp32/release_provenance.py')
    provenance=importlib.util.module_from_spec(spec);spec.loader.exec_module(provenance)
    expected_inputs=provenance.inputs(ROOT)
    builds={};deliveries=[]
    for board in args.boards:
        build=ROOT/'.pio/build'/('ut_meter_'+board);app=build/'firmware.bin'
        assert app.is_file(),'Missing build: '+board
        manifest=build/'release-inputs.json'
        assert manifest.is_file(),'Build provenance missing; rebuild '+board
        built=json.loads(manifest.read_text())
        assert built['inputs']==expected_inputs,'Build inputs differ; rebuild all targets (clean first if necessary)'
        assert built['app_sha256']==sha(app) and built['board_header_sha256']==sha(build/'meter_board_config.h'),'Build artifact mismatch: '+board
        assert ('#define METER_ID "'+board+'"') in (build/'meter_board_config.h').read_text()
        if board=='jc3248w535':assert app.stat().st_size<0x200000,'App exceeds retained 2MiB JC partition'
        folder=out/board;folder.mkdir(exist_ok=True)
        for src,name in ((app,'app.bin'),(build/'bootloader.bin','bootloader.bin'),(build/'partitions.bin','partitions.bin'),(boot_app0,'boot_app0.bin'),(build/'meter_board_config.h','meter_board_config.h'),(manifest,'release-inputs.json')):shutil.copy2(src,folder/name)
        # Arduino's QIO runtime uses a DIO bootloader header. Keep the exact
        # SDK-built headers instead of forcing a different boot flash mode.
        subprocess.run([sys.executable,str(args.esptool),'--chip','esp32s3','merge_bin','-o',str(folder/'factory.bin'),'--flash_mode','keep','--flash_freq','keep','--flash_size','keep','0x0',str(folder/'bootloader.bin'),'0x8000',str(folder/'partitions.bin'),'0xe000',str(folder/'boot_app0.bin'),'0x10000',str(folder/'app.bin')],cwd=ROOT,check=True,capture_output=True,text=True)
        factory=(folder/'factory.bin').read_bytes()
        for offset,name in ((0,'bootloader.bin'),(0x8000,'partitions.bin'),(0xe000,'boot_app0.bin'),(0x10000,'app.bin')):
            blob=(folder/name).read_bytes();assert factory[offset:offset+len(blob)]==blob,name
        hardware_note=('JC hardware exercised on earlier matching application code; physical edge confirmation pending' if board=='jc3248w535' else 'See docs/WAVESHARE_LCD349.md for V2 bring-up evidence and remaining physical checks' if board=='lcd349_v2' else 'Build and host-test evidence only; physical board qualification pending')
        info=dict(board=board,source_commit=commit,version=version,app_bytes=app.stat().st_size,app_sha256=sha(app),factory_sha256=sha(folder/'factory.bin'),app_offset='0x10000',factory_offset='0x0',flash='16MB',psram='8MB OPI',validation='SDK build passed; image/source hashes verified. Packaging does not flash hardware.',hardware_note=hardware_note)
        (folder/'BUILDINFO.json').write_text(json.dumps(info,indent=2));builds[board]=info
        install=f'''# {board} — {version}

Use only with the exact matching board/controller/pins in `meter_board_config.h`.
These builds require ESP32-S3, 16MB flash and 8MB OPI PSRAM. A USB chip name alone
does not identify a display board. See the repository hardware table before use.

For lcd349_v1/lcd349_v2, match the physical PCB revision: V2 has Rev1.1 on the
PCB or V2 on its QC label. Reset and backlight pins differ. RTC/IMU integration
and one-bit SDMMC are included. See docs/WAVESHARE_LCD349.md for V2 bring-up
evidence and remaining LCD/touch, RTC retention, IMU-axis and card checks in
the source archive. Older v0.1.0 release packages do not include these targets.

## Fresh installation

Back up any existing firmware/settings first. `factory.bin` replaces the bootloader,
partition table and application, and resets saved settings in the covered NVS area.
Use it for a fresh compatible board, not an upgrade that must preserve settings.
It does not format an external TF card. Install the Python tool with
`python -m pip install esptool==4.9.0`, close serial monitors, and substitute the
confirmed port (for example COM47 or /dev/ttyACM0) for PORT:

```sh
python -m esptool --chip esp32s3 --port PORT --baud 460800 write_flash 0x0 factory.bin
python -m esptool --chip esp32s3 --port PORT verify_flash 0x0 factory.bin
```

## App-only upgrade

Preserve your original partition table and NVS. This requires an existing app0 at
0x10000 that is the active boot-selected application, with enough capacity for the
app byte count in BUILDINFO.json. A vendor OTA setup may instead boot app1; confirm
the active slot before using this command and do not erase OTA metadata to guess.
The original
JC 2MiB slot is supported by this release. Do not assume another vendor partition
layout matches; use a fresh-install workflow only after backing it up.

```sh
python -m esptool --chip esp32s3 --port PORT --baud 460800 write_flash 0x10000 app.bin
python -m esptool --chip esp32s3 --port PORT verify_flash 0x10000 app.bin
```

Never write `app.bin` at0x0 or write `factory.bin` at0x10000. No erase_flash is
required. If needed, hold BOOT while tapping RESET, release BOOT, then retry using
the bootloader's confirmed port. Release BOOT and tap RESET after verification.
On first JC boot without saved calibration, follow the four corner targets and
centre verification. If a valid calibration exists on an app-only upgrade, it
loads from NVS. Full instructions: ../../docs/USER_GUIDE.md in the source archive,
or https://github.com/siujacky/Esp32-s3-Bluetooth-datalogger/blob/main/docs/USER_GUIDE.md

Checksums are in SHA256SUMS.txt. BUILDINFO.json records exact source and image hashes.
'''
        (folder/'INSTALL.md').write_text(install,encoding='utf-8')
        names=['app.bin','bootloader.bin','partitions.bin','boot_app0.bin','meter_board_config.h','release-inputs.json','factory.bin','BUILDINFO.json','INSTALL.md']
        files=[folder/name for name in names]
        sums_file=folder/'SHA256SUMS.txt'
        sums_file.write_text(''.join(sha(p)+'  '+p.name+'\n' for p in files))
        files.append(sums_file)
        archive=out/(board+'-'+version+'.zip')
        with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED) as z:
            for p in files:z.write(p,p.name)
        with zipfile.ZipFile(archive) as z:
            assert z.testzip() is None
            assert set(z.namelist())=={p.name for p in files}
            for p in files:assert hashlib.sha256(z.read(p.name)).hexdigest()==sha(p)
        deliveries.append(archive)
    source=out/('source-'+version+'.zip')
    subprocess.run(['git','archive','--format=zip','--prefix=Esp32-s3-Bluetooth-datalogger-'+version+'/','-o',str(source),commit],cwd=ROOT,check=True)
    with zipfile.ZipFile(source) as z:assert z.testzip() is None
    deliveries.append(source)
    manual=out/'README.html';shutil.copy2(ROOT/'src/esp32_touch/meter/README.html',manual);deliveries.append(manual)
    info=out/'BUILDINFO.json';info.write_text(json.dumps(dict(version=version,source_commit=commit,boards=builds),indent=2));deliveries.append(info)
    sums=out/'SHA256SUMS.txt';sums.write_text(''.join(sha(p)+'  '+p.name+'\n' for p in deliveries))
    print(json.dumps(dict(source_commit=commit,output=str(out),assets=[p.name for p in deliveries]+[sums.name],verified=True),indent=2))
if __name__=='__main__':main()
