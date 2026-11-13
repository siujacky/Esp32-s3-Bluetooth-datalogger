"""Check documentation links/images without external packages or network access."""
from pathlib import Path
from urllib.parse import unquote,urlsplit
import re,sys
ROOT=Path(__file__).resolve().parents[1]
def heading_ids(text):
    seen={};ids=set()
    for name in re.findall(r'^#{1,6}\s+(.+?)\s*#*$',text,re.M):
        name=re.sub(r'[^\w\s-]','',name.lower()).replace(' ','-')
        count=seen.get(name,0);seen[name]=count+1;ids.add(name if not count else name+'-'+str(count))
    return ids
def main():
    files=[ROOT/'README.md',ROOT/'THIRD_PARTY.md',*sorted((ROOT/'docs').rglob('*.md')),ROOT/'src/esp32_touch/meter/README.md']
    errors=[];checked=0
    for file in files:
        if not file.is_file():errors.append(str(file.relative_to(ROOT))+': missing document');continue
        text=file.read_text(encoding='utf-8');body=re.sub(r'```.*?```','',text,flags=re.S)
        links=re.findall(r'!?\[[^\]]*\]\(([^\s)]+)(?:\s+"[^"]*")?\)',body)+re.findall(r'(?:src|href)="([^"]+)"',body)
        for raw in links:
            parsed=urlsplit(raw)
            if parsed.scheme or raw.startswith('//'):continue
            target=(file.parent/unquote(parsed.path)).resolve() if parsed.path else file
            checked+=1
            if not target.is_relative_to(ROOT) or not target.exists():errors.append(f'{file.relative_to(ROOT)}: missing target {raw}');continue
            if parsed.fragment and target.suffix=='.md' and unquote(parsed.fragment) not in heading_ids(target.read_text(encoding='utf-8')):errors.append(f'{file.relative_to(ROOT)}: missing heading {raw}')
        if re.search(r'C:[/\\](?:RC|Users)[/\\]',body):errors.append(str(file.relative_to(ROOT))+': machine-specific source path')
    if errors:print('\n'.join(errors));return 1
    print(f'PASS {len(files)} documents; {checked} local links, anchors and images; no machine-specific source paths');return 0
if __name__=='__main__':sys.exit(main())
