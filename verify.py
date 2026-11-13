"""verify.py -- your compressed sizes against the reference run (expected/). Sizes do not depend on the machine:
shz, shzx, shza, shzb and bsc (pinned commit) must match to the byte. Classic codecs and Parquet match when your versions
are the ones in expected/VERSIONS.txt. Speeds differ by machine and are not compared."""
import csv, glob, os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
def load(d):
    m = {}
    for p in glob.glob(os.path.join(HERE, d, '*.csv')):
        for r in csv.DictReader(open(p)): m[(r['category'] if r['category'] != 'wildchat' else 'ai', r['file'], r['codec'])] = int(r['comp_bytes'])
    return m
mine, ref = load('results'), load('expected')
det = lambda c: c.startswith(('shz', 'bsc'))
same = [k for k in mine if k in ref and mine[k] == ref[k]]
diff = [k for k in mine if k in ref and mine[k] != ref[k]]
print('compared %d sizes with the reference: %d identical, %d different' % (len(same) + len(diff), len(same), len(diff)))
bad = [k for k in diff if det(k[2])]
for k in diff[:20]: print('  %s %s %s: yours %d, reference %d%s' % (k[0], k[1], k[2], mine[k], ref[k], '  <-- must match' if det(k[2]) else '  (version-dependent)'))
print('VERDICT: %s' % ('REPRODUCED (every shz and bsc size identical)' if same and not bad else 'NOT REPRODUCED' if bad else 'nothing to compare yet'))
sys.exit(1 if bad else 0)
