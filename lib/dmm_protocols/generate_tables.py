# SPDX-License-Identifier: GPL-3.0-or-later
"""Regenerate literal tables from the pinned dmm-tools source (first argument)."""
from pathlib import Path
import re, json, sys
root=Path(sys.argv[1])/ 'crates/dmm-lib/src/protocol'
out=Path(__file__).resolve().parent
q=lambda s:json.dumps(re.sub(r'\\u\{([0-9a-fA-F]+)\}',lambda m:chr(int(m[1],16)),s),ensure_ascii=False)
strip=lambda s:re.sub(r'//[^\n]*','',s)
modes=['AcV','AcMv','DcV','DcMv','Hz','DutyCycle','Ohm','Continuity','Diode','Capacitance','TempC','TempF','DcUa','AcUa','DcMa','AcMa','DcA','AcA','Hfe','Live','Ncv','LozV','ClampAcA','ClampDcA','LpfV','AcDcV','LpfA','AcDcA','ClampLpfA','ClampAcDcA','Inrush']
lines=['// SPDX-License-Identifier: GPL-3.0-or-later','// Generated from dmm-tools 2d4dc507; do not hand-edit.','static const UniRange uniRanges[]={']
for model,file in [(61,'ut61e_plus'),(62,'ut61b_plus'),(63,'ut61d_plus'),(60,'ut60bt'),(202,'ut202bt')]:
    s=strip((root/'ut61eplus/tables'/f'{file}.rs').read_text(encoding='utf8'))
    arrays={}
    for name,body in re.findall(r'static (\w+):\s*\[RangeInfo;\s*\d+\]\s*=\s*\[(.*?)\];',s,re.S):
        arrays[name]=[(m[1],m[2]) if m[0].startswith('r(') else ('','') for m in re.findall(r'(r\("([^"]*)",\s*"([^"]*)"\)|GAP)',body)]
    entry=s[s.index('fn entry('):];entry=entry[:entry.index('#[cfg') if '#[cfg' in entry else len(entry)]
    for arms,name in re.findall(r'((?:Mode::\w+\s*\|?\s*)+)\s*=>\s*&(\w+)',entry):
        for mode in re.findall(r'Mode::(\w+)',arms):
            for i,(label,unit) in enumerate(arrays[name]):
                lines.append('{%d,%d,%d,%s},'%(model,modes.index(mode),i,q(unit)))
lines+=['};'];(out/'uni_ranges.inc').write_text('\n'.join(lines)+'\n',encoding='utf8')
s=strip((root/'eevblog121gw/tables.rs').read_text(encoding='utf8'))
arrays={name:re.findall(r'r\((\d+),\s*"([^"]*)",\s*"([^"]*)"\)',body) for name,body in re.findall(r'const (\w+): &\[Range\] = &\[(.*?)\];',s,re.S)}
models=re.search(r'static MODES:.*?=\s*\[(.*?)\];',s,re.S)[1]
lines=['// SPDX-License-Identifier: GPL-3.0-or-later','// Generated from dmm-tools 2d4dc507.','static const GwRange gwRanges[]={']
for mode,(name,arr) in enumerate(re.findall(r'm\("([^"]*)",\s*(\w+)\)',models)):
    for i,(dp,unit,label) in enumerate(arrays[arr]):lines.append('{%d,%d,%s,%s,%s},'%(mode,i,dp,q(name),q(unit)))
lines+=['};'];(out/'gw_ranges.inc').write_text('\n'.join(lines)+'\n',encoding='utf8')
s=strip((root/'zotek/layout.rs').read_text(encoding='utf8'));lines=['// SPDX-License-Identifier: GPL-3.0-or-later','// Generated from dmm-tools 2d4dc507.','static const ZBit zbits[]={']
units=['Volt','Amp','Ohm','Farad','Hertz','Percent','Celsius','Fahrenheit'];prefix=['Nano','Micro','Milli','Kilo','Mega'];allowed={'FARAD':8,'VOLT':1,'AMP':2,'OHM_HZ':20,'VOLT_AMP_FARAD':11}
for name,body in re.findall(r'static (ZT\w+): Layout = Layout \{(.*?)\n\};',s,re.S):
    typ=int(re.search(r'type_byte: (\d+)',body)[1])
    for byte,bit,meaning in re.findall(r'bit\((\d+),\s*(\d+),\s*Meaning::(.*)\),',body):
        if meaning.startswith('Unit('):kind=0;arg=units.index(re.search(r'Unit::(\w+)',meaning)[1]);mask=0
        elif meaning.startswith('Prefix('):kind=1;arg=prefix.index(re.search(r'Prefix::(\w+)',meaning)[1]);mask=allowed[re.search(r',\s*(\w+)\)',meaning)[1]]
        else:kind=['Ac','Dc','Diode','Continuity','Hold','Rel','AutoRange','Min','Max','LowBattery','Peak','Inrush','OverVoltage','SecondaryPercent','SecondaryHertz','SecondaryKilo','Silent'].index(meaning)+2;arg=mask=0
        lines.append('{%d,%s,%d,%d,%d,%d},'%(typ,byte,1<<int(bit),kind,arg,mask))
lines+=['};'];(out/'zotek_bits.inc').write_text('\n'.join(lines)+'\n',encoding='utf8')
print('Generated UNI ranges, 121GW ranges and ZOTEK annunciator maps')

