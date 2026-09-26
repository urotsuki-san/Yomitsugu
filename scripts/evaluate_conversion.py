"""同梱辞書を使い、固定入力の変換候補と所要時間を測る。"""
import argparse
import json
import os
from pathlib import Path
import subprocess
import statistics

ROOT = Path(__file__).resolve().parents[1]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--exe', type=Path, default=ROOT/'native/build/Release/debug_dump.exe')
    parser.add_argument('--check', action='store_true', help='Fail on regression of the measured acceptance floors')
    parser.add_argument('--baseline', type=Path, help='Also preserve each previously passing first/top-five result')
    args = parser.parse_args()
    rows = [line.split('\t') for line in (ROOT/'native/tests/conversion_cases.tsv').read_text(encoding='utf-8').splitlines() if line and not line.startswith('#')]
    proc = subprocess.run([str(args.exe.resolve()), '--probe'], input='\n'.join(row[1] for row in rows)+'\n', encoding='utf-8', capture_output=True, timeout=240, env={**os.environ, 'PYTHONUTF8':'1'})
    if proc.returncode:
        raise SystemExit(f'Probe failed: {proc.returncode}')
    probes = [line.split('\t') for line in proc.stdout.splitlines() if line.startswith('PROBE\t')]
    if len(probes) != len(rows):
        raise SystemExit(f'Incomplete probe output: {len(probes)}/{len(rows)}')
    results=[]
    for (group,raw,expected), probe in zip(rows, probes):
        assert probe[1] == raw
        candidates=probe[5:]
        results.append(dict(group=group, raw=raw, expected=expected, reading=probe[2], rank=candidates.index(expected)+1 if expected in candidates else None, ms=float(probe[4]), candidates=candidates))
    groups={}
    for group in sorted({r['group'] for r in results}):
        values=[r for r in results if r['group']==group]
        groups[group]=dict(total=len(values), top1=sum(r['rank']==1 for r in values), top5=sum(r['rank'] is not None and r['rank']<=5 for r in values), top16=sum(r['rank'] is not None for r in values))
    times=sorted(r['ms'] for r in results[1:])
    report=dict(groups=groups, warm_ms=dict(median=statistics.median(times),p95=times[int((len(times)-1)*.95)],maximum=max(times)),results=results)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({k:v for k,v in report.items() if k!='results'},ensure_ascii=True,indent=2))
    failures=[]
    if args.check:
        floors={'loanword':(30,31), 'typo':(6,20), 'general':(14,16), 'mixed':(3,3), 'preserve':(5,5), 'sentence':(6,7)}
        for group,(top1,top5) in floors.items():
            if groups[group]['top1']<top1 or groups[group]['top5']<top5:
                failures.append(group)
        for raw in ('aninsuto-ru','anninsuto-ru','insuto-ru','konpyu-ta-','sofutowea','ha-dowea'):
            if next(r for r in results if r['raw']==raw)['rank']!=1: failures.append(raw)
    if args.baseline:
        previous={r['raw']:r['rank'] for r in json.loads(args.baseline.read_text(encoding='utf-8'))['results']}
        for result in results:
            old=previous[result['raw']]
            if old==1 and result['rank']!=1:
                failures.append(result['raw'])
            elif old is not None and old<=5 and (result['rank'] is None or result['rank']>5):
                failures.append(result['raw'])
    if failures: raise SystemExit('Conversion quality regression: '+', '.join(failures))

if __name__ == '__main__':
    main()
