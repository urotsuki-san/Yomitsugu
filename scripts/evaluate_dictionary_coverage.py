"""Deterministic dictionary coverage probe; not an accuracy benchmark."""
from pathlib import Path
import argparse, gzip, hashlib, importlib.util, json, re, subprocess

root = Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source', type=Path, required=True, help='Official UTF-8 edict2u.gz snapshot')
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--exe', type=Path, default=root/'native/build/Release/debug_dump.exe')
args=parser.parse_args()
spec = importlib.util.spec_from_file_location('updater', root/'scripts/update_public_dictionary.py')
updater = importlib.util.module_from_spec(spec)
spec.loader.exec_module(updater)
reverse = {}
for key, kana, pending in re.findall(r'\{u8"([^"]*)", u8"([^"]*)", u8"([^"]*)"\}', (root/'native/src/romaji_table.inc').read_text(encoding='utf-8')):
    if not pending and re.fullmatch('[a-z-]+', key) and re.fullmatch('[ぁ-ゖー]+', kana):
        if kana not in reverse or len(key) < len(reverse[kana]): reverse[kana] = key

def romanize(kana):
    chunks=[]
    while kana:
        if kana[0]=='ん': chunks.append('N'); kana=kana[1:]; continue
        if kana[0]=='っ': chunks.append('Q'); kana=kana[1:]; continue
        match=next((part for part in sorted(reverse,key=len,reverse=True) if kana.startswith(part)),None)
        if match is None: return None
        chunks.append(reverse[match]); kana=kana[len(match):]
    result=''
    for chunk in reversed(chunks):
        if chunk=='N': chunk=('nn' if not result else "n'" if result[0] in 'aiueoyn' else 'n')
        if chunk=='Q': chunk=result[0] if result and result[0] in 'bcdfghjkpqrstvwxz' else 'xtu'
        result=chunk+result
    return result

pool={}
for line in gzip.decompress(args.source.read_bytes()).decode('utf-8').splitlines():
    if '(P)' not in line or ' /' not in line: continue
    head,glosses=line.split(' /',1)
    for reading,word,_ in updater.lexical_entries(head,glosses):
        if 3<=len(reading)<=15 and re.fullmatch('[ァ-ヶー]{3,30}',word): pool.setdefault(reading,set()).add(word)
keys=sorted(pool,key=lambda key:hashlib.sha256(('Yomitsugu-20260926:'+key).encode()).hexdigest())[:500]
rows=[(key,romanize(key),sorted(pool[key])) for key in keys]
assert all(raw for _,raw,_ in rows)
proc=subprocess.run([str(args.exe.resolve()),'--probe'],input='\n'.join(raw for _,raw,_ in rows)+'\n',encoding='utf-8',capture_output=True,timeout=360)
assert proc.returncode==0, proc.stderr[-1000:]
probes=[line.split('\t') for line in proc.stdout.splitlines() if line.startswith('PROBE\t')]
assert len(probes)==len(rows)
results=[]
for (reading,raw,expected),probe in zip(rows,probes):
    assert probe[1]==raw
    candidates=probe[5:]
    ranks=[candidates.index(value)+1 for value in expected if value in candidates]
    results.append(dict(reading=reading,raw=raw,expected=expected,reading_match=probe[2]==reading,rank=min(ranks) if ranks else None,candidates=candidates))
summary=dict(pool_readings=len(pool),sample=len(rows),reading_roundtrip=sum(row['reading_match'] for row in results),top1=sum(row['rank']==1 for row in results),top5=sum(row['rank'] is not None and row['rank']<=5 for row in results),top16=sum(row['rank'] is not None for row in results))
report=dict(source_url='https://www.edrdg.org/pub/Nihongo/edict2u.gz', source_sha256=hashlib.sha256(args.source.read_bytes()).hexdigest(), dictionary_data_license='EDRDG-derived entries: CC BY-SA 4.0; see native/third_party/public-dictionary-NOTICE.txt', sample_seed='Yomitsugu-20260926:', method='500 deterministic SHA-256 sampled common EDICT2 katakana readings, expected any source headword. Dictionary coverage only; no contextual intent, not independent of dictionary data.',summary=summary,results=results)
args.output.parent.mkdir(parents=True,exist_ok=True)
args.output.write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print(json.dumps(summary))
for row in results:
    if not row['reading_match'] or row['rank'] is None or row['rank']>5: print(json.dumps(row,ensure_ascii=True))
