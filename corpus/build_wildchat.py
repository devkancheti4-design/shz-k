"""AI-transcript corpus from one public WildChat-1M shard (allenai, ODC-BY): every conversation as one JSON line
(all fields as published), cut into 10 MB batches -- the way an AI service would store its transcripts. 14 batches:
7 design, 7 sealed, by random.Random(20261113). The parquet is deleted afterwards (disk is short).
    python3 -I corpus/build_wildchat.py data/wildchat/train-00000-of-00014.parquet"""
import json, os, random, sys, hashlib
import pyarrow.parquet as pq
src = sys.argv[1]
out = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'wildchat')
os.makedirs(out, exist_ok=True)
pf = pq.ParquetFile(src)
batches, cur = [], []
size = 0
for rg in range(pf.num_row_groups):
    for row in pf.read_row_group(rg).to_pylist():
        line = json.dumps(row, ensure_ascii=False, default=str) + '\n'
        cur.append(line); size += len(line.encode())
        if size >= 10_000_000:
            batches.append(''.join(cur).encode()); cur, size = [], 0
            if len(batches) == 14: break
    if len(batches) == 14: break
idx = list(range(len(batches))); random.Random(20261113).shuffle(idx)
man = []
for rank, i in enumerate(idx):
    split = 'design' if rank < 7 else 'sealed'
    d = os.path.join(out, split); os.makedirs(d, exist_ok=True)
    name = 'wildchat_%02d.jsonl' % i
    open(os.path.join(d, name), 'wb').write(batches[i])
    man.append((split, name, len(batches[i]), hashlib.sha256(batches[i]).hexdigest()))
with open(os.path.join(os.path.dirname(out), 'WILDCHAT_MANIFEST.tsv'), 'w') as f:
    f.write('split\tfile\tbytes\tsha256\n'); [f.write('%s\t%s\t%d\t%s\n' % m) for m in sorted(man)]
print(len(batches), 'batches;', sum(m[2] for m in man), 'bytes')
