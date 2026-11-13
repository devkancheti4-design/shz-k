"""Cut corpus/aiapi/{embeddings,logprobs}_all.jsonl into 10 MB batches at line boundaries; 2 design + 2 sealed each,
by random.Random(20261113). Writes corpus/aiapi/<split>/<kind>_NN.jsonl and corpus/AIAPI_MANIFEST.tsv."""
import hashlib, os, random
D = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'aiapi'); man = []
for kind in ('embeddings', 'logprobs'):
    lines = open(os.path.join(D, kind + '_all.jsonl'), 'rb').read().split(b'\n')
    batches, cur, size = [], [], 0
    for l in lines:
        if not l: continue
        cur.append(l + b'\n'); size += len(l) + 1
        if size >= 10_000_000: batches.append(b''.join(cur)); cur, size = [], 0
    idx = list(range(len(batches))); random.Random(20261113).shuffle(idx)
    for r, i in enumerate(idx[:4]):
        split = 'design' if r < 2 else 'sealed'
        os.makedirs(os.path.join(D, split), exist_ok=True)
        name = '%s_%02d.jsonl' % (kind, i); open(os.path.join(D, split, name), 'wb').write(batches[i])
        man.append((split, name, len(batches[i]), hashlib.sha256(batches[i]).hexdigest()))
with open(os.path.join(os.path.dirname(D), 'AIAPI_MANIFEST.tsv'), 'w') as f:
    f.write('split\tfile\tbytes\tsha256\n'); [f.write('%s\t%s\t%d\t%s\n' % m) for m in sorted(man)]
for m in sorted(man): print(m[0], m[1], m[2])
