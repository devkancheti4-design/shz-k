"""parquet_ref.py -- the industry's answer: the same JSON lines stored as Parquet (pyarrow), zstd-compressed.
    python3 bench/parquet_ref.py OUT.csv SPLIT CATEGORY FILE...
Every float column is cast to float32 (lossless here: every float in these files is exactly a float32; checked), the
best case for Parquet. Three settings: zstd level 3; zstd level 19; zstd level 19 with BYTE_STREAM_SPLIT for float
columns (the recommended encoding for floats). Times: compress = JSON parse + Arrow table + Parquet write; decompress =
Parquet read + rows + JSON text again (transcripts arrive and leave as JSON). ok = 1 if the JSON comes back byte-exact."""
import csv, io, json, os, sys, time, struct
import pyarrow as pa, pyarrow.parquet as pq

def f32_schema(t):
    if pa.types.is_floating(t): return pa.float32()
    if pa.types.is_list(t) or pa.types.is_large_list(t): return pa.list_(f32_schema(t.value_type))
    if pa.types.is_struct(t): return pa.struct([pa.field(f.name, f32_schema(f.type), f.nullable) for f in t])
    return t

def float_cols(schema, prefix=''):
    out = []
    for f in schema:
        name = prefix + f.name
        t = f.type
        while pa.types.is_list(t): t = t.value_type; name += '.list.element'
        if pa.types.is_floating(t): out.append(name)
        elif pa.types.is_struct(t): out += float_cols(t, name + '.')
    return out

def main():
    outp, split, cat = sys.argv[1:4]
    new = not os.path.exists(outp)
    with open(outp, 'a', newline='') as fo:
        w = csv.writer(fo)
        if new: w.writerow(['split', 'category', 'file', 'bytes', 'codec', 'comp_bytes', 'ctime_s', 'dtime_s', 'ok'])
        for path in sys.argv[4:]:
            raw = open(path, 'rb').read()
            lines = raw.decode('utf-8').split('\n')
            if lines and lines[-1] == '': lines = lines[:-1]
            for name, level, bss in (('parquet-zstd3', 3, False), ('parquet-zstd19', 19, False), ('parquet-zstd19-bss', 19, True)):
                t0 = time.perf_counter()
                rows = [json.loads(l) for l in lines]
                tab = pa.Table.from_pylist(rows)
                tab = tab.cast(pa.schema([pa.field(f.name, f32_schema(f.type), f.nullable) for f in tab.schema]))
                buf = io.BytesIO()
                kw = dict(compression='zstd', compression_level=level)
                if bss:
                    cols = float_cols(tab.schema)
                    kw.update(use_byte_stream_split=cols, use_dictionary=[c.name for c in tab.schema if c.name not in cols])
                pq.write_table(tab, buf, **kw)
                data = buf.getvalue(); ct = time.perf_counter() - t0
                t1 = time.perf_counter()
                back = pq.read_table(io.BytesIO(data)).to_pylist()
                text = '\n'.join(json.dumps(r, ensure_ascii=False) for r in back) + '\n'
                dt = time.perf_counter() - t1
                ok = int(text.encode('utf-8') == raw)
                w.writerow([split, cat, os.path.basename(path), len(raw), name, len(data), round(ct, 4), round(dt, 4), ok]); fo.flush()
                print('%-28s %-20s %10d -> %9d  c %.2fs  d %.2fs  byte-exact JSON: %s' % (os.path.basename(path), name, len(raw), len(data), ct, dt, 'yes' if ok else 'no'), flush=True)

if __name__ == '__main__':
    main()
