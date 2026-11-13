"""report.py -- every table and verdict from results/*.csv, priced with YOUR prices.json.
    python3 report.py [results_dir]     (default: results; use 'expected' to see the reference run)
Total cost per TB of raw data = CPU (one compression + one decompression per read, at least one) + storage of the
compressed bytes for the retention period. A codec is on the frontier if no other codec is both smaller and faster."""
import csv, glob, json, os, sys
from collections import defaultdict
HERE = os.path.dirname(os.path.abspath(__file__))
D = os.path.join(HERE, sys.argv[1] if len(sys.argv) > 1 else 'results')
P = json.load(open(os.path.join(HERE, 'prices.json')))
cpu, store, months, reads = P['cpu_usd_per_core_hour'], P['storage_usd_per_gb_month'], P['retention_months'], P['reads_per_month']
data = defaultdict(lambda: defaultdict(lambda: dict(n=0, z=0, c=0.0, d=0.0, ok=True, files=0)))
for p in sorted(glob.glob(os.path.join(D, '*.csv'))):
    for r in csv.DictReader(open(p)):
        a = data[r['category']][r['codec']]
        a['n'] += int(r['bytes']); a['z'] += int(r['comp_bytes']); a['c'] += float(r['ctime_s']); a['d'] += float(r['dtime_s'])
        a['ok'] &= r['ok'] == '1'; a['files'] += 1
def tco(a, m):
    tb = 1e12 / a['n']; nread = max(1.0, reads * m)
    return tb * (a['c'] + a['d'] * nread) / 3600 * cpu + a['z'] * tb / 1e9 * store * m
out = ['# shz -- results priced with prices.json', '',
       'Prices: CPU $%.4f per core-hour, storage $%.4f per GB-month, reads per month %s. Results from `%s/`.' % (cpu, store, reads, os.path.basename(D)), '']
for cat in sorted(data):
    A = data[cat]; ref = min(A, key=lambda k: A[k]['z'])
    out += ['## %s (%d files, %s bytes)' % (cat, max(a['files'] for a in A.values()), format(max(a['n'] for a in A.values()), ',')), '',
            '| codec | bytes | ratio | compress MB/s | decompress MB/s | round trip | ' + ' | '.join('$/TB, %d mo' % m for m in months) + ' | frontier |',
            '|---|---:|---:|---:|---:|---|' + '---:|' * len(months) + '---|']
    for k, a in sorted(A.items(), key=lambda kv: kv[1]['z']):
        front = not any(o is not a and o['z'] < a['z'] and o['c'] < a['c'] for o in A.values())
        out.append('| %s | %s | %.2f | %.2f | %.2f | %s | %s | %s |' % (k, format(a['z'], ','), a['n'] / a['z'], a['n'] / a['c'] / 1e6, a['n'] / a['d'] / 1e6,
                   'exact' if a['ok'] else 'FAILED', ' | '.join('%.2f' % tco(a, m) for m in months), 'yes' if front else ''))
    out += [''] + ['- cheapest at %d months: **%s**' % (m, min(A, key=lambda k: tco(A[k], m))) for m in months] + ['- smallest: **%s**' % ref, '']
text = '\n'.join(out) + '\n'
open(os.path.join(HERE, 'report.md'), 'w').write(text); print(text)
