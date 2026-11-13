# SPDX-License-Identifier: GPL-3.0-or-later
"""Generate checked-in tests from upstream real packet golden fixtures."""
from pathlib import Path
import sys,json,yaml,re
root=Path(sys.argv[1])/'crates/dmm-lib/tests/golden';out=Path(__file__).parent
q=lambda s:json.dumps(str(s),ensure_ascii=False)
lines=['// SPDX-License-Identifier: GPL-3.0-or-later','// Real upstream golden packets, dmm-tools 2d4dc507.','static const Golden goldens[]={']
for folder,profile in [('ut61eplus','ut61eplus'),('ut61b+','ut61b+'),('ut181a','ut181a')]:
 for f in sorted((root/folder).glob('*.yaml')):
  y=yaml.safe_load(f.read_text(encoding='utf8'));raw=bytes.fromhex(y['raw_hex']);val=str(y['value']);unit=y['unit'];channel=0
  if val=='':
   if not y.get('aux'):continue
   val=str(y['aux'][0]['value']);unit=y['aux'][0].get('unit',unit);channel=1
  numeric=True
  try:float(val)
  except:numeric=False
  expected='NAN' if not numeric else val
  flags=0
  for k,b in [('hold',0),('rel',1),('min',2),('max',3),('low_battery',4),('hv_warning',6),('auto_range',8),('dc',9),('avg',11),('record',12),('loz',13),('lead_error',14),('comp',15),('peak_min',16),('peak_max',17)]:
   if y.get('flags',{}).get(k):flags|=1<<b
  lines.append('{%s,%s,{%s},%d,%s,%s,%s,%s,%d,%d},'%(q(folder+'/'+f.stem),q(profile),','.join('0x%02x'%b for b in raw),len(raw),'true' if numeric else 'false',expected,q(unit),q(y['mode']),channel,flags))
lines+=['};'];(out/'goldens.inc').write_text('\n'.join(lines)+'\n',encoding='utf8');print('generated',len(lines)-4,'goldens')
# literal protocol examples from upstream; take arrays without manufacturing input data
base=Path(sys.argv[1])/'crates/dmm-lib/src/protocol'
lines=['// SPDX-License-Identifier: GPL-3.0-or-later','// Upstream worked and community packet vectors.']
for name,file,begin,end,size in [('gwVectors','eevblog121gw/packet.rs','pub(crate) const EXAMPLES','/// Every example',19),('owVectors','owon/frame15.rs','pub(crate) const VECTORS','    ];',15)]:
 s=(base/file).read_text(encoding='utf8');s=s[s.index(begin):];s=s[:s.index(end)+(len(end) if end=='    ];' else 0)]
 # initializer arrays only, excluding Rust dimension fields
 arrays=re.findall(r'\[\s*((?:0x[0-9A-Fa-f]+,?\s*)+)\]',s)
 vals=[re.findall(r'0x[0-9A-Fa-f]+',a) for a in arrays];vals=[a for a in vals if len(a)==size]
 lines.append('static const uint8_t %s[][%d]={'%(name,size));lines+=['{'+','.join(a)+'},' for a in vals];lines.append('};')
(out/'vectors.inc').write_text('\n'.join(lines)+'\n',encoding='utf8')

# Independent source test matrices, separate from the product bit-map tables.
s=(base/'zotek/layout.rs').read_text(encoding='utf8')
lines=['// SPDX-License-Identifier: GPL-3.0-or-later','// Independent upstream per-annunciator test cases, dmm-tools 2d4dc507.','static const ZCase zCases[]={']
flagmap={'hold':1,'rel':2,'min':4,'max':8,'low_battery':16,'hv_warning':64,'auto_range':256,'dc':512}
for t in [1,2,3,4]:
 start=s.index(f'fn type{t}_every_annunciator()');block=s[start:];block=block[:block.index('];')+2]
 pattern=r'\(\s*&\[((?:\s*\(\d+,\s*0x[0-9A-Fa-f]+\),?\s*)+)\],\s*"([^"]*)",\s*(?:0x[0-9A-Fa-f]+,\s*)?"([^"]*)",\s*&\[([^\]]*)\],?\s*\)'
 for bits,mode,unit,flags in re.findall(pattern,block,re.S):
  ann=[0]*19
  for byte,mask in re.findall(r'\((\d+),\s*(0x[0-9A-Fa-f]+)\)',bits):ann[int(byte)]|=int(mask,16)
  flag=sum(flagmap[f] for f in re.findall(r'"([^"]+)"',flags))
  lines.append('{%d,{%s},%s,%s,%d},'%(t,','.join(str(b) for b in ann),q(mode),q(unit),flag))
lines.append('};');(out/'zotek_annunciators.inc').write_text('\n'.join(lines)+'\n',encoding='utf8')
