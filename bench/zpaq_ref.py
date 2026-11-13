"""zpaq_ref.py -- zpaq -m5 (context mixing) from its command line: archive size, wall time to add and to extract,
and a byte-exact check of the extracted file.   python3 bench/zpaq_ref.py OUT.csv SPLIT CATEGORY FILE..."""
import csv, os, shutil, subprocess, sys, tempfile, time
outp, split, cat = sys.argv[1:4]
new = not os.path.exists(outp)
with open(outp, 'a', newline='') as f:
    w = csv.writer(f)
    if new: w.writerow(['split', 'category', 'file', 'bytes', 'codec', 'comp_bytes', 'ctime_s', 'dtime_s', 'ok'])
    for path in sys.argv[4:]:
        d = tempfile.mkdtemp(prefix='zpq_'); src = os.path.join(d, 'f'); shutil.copyfile(path, src)
        arc = os.path.join(d, 'a.zpaq')
        t0 = time.perf_counter(); subprocess.run(['zpaq', 'a', arc, 'f', '-m5'], cwd=d, capture_output=True, check=True); ct = time.perf_counter() - t0
        os.makedirs(os.path.join(d, 'x'))
        t0 = time.perf_counter(); subprocess.run(['zpaq', 'x', arc, '-to', 'x'], cwd=d, capture_output=True, check=True); dt = time.perf_counter() - t0
        ok = open(os.path.join(d, 'x', 'f'), 'rb').read() == open(path, 'rb').read()
        w.writerow([split, cat, os.path.basename(path), os.path.getsize(path), 'zpaq-m5', os.path.getsize(arc), round(ct, 4), round(dt, 4), int(ok)])
        f.flush(); shutil.rmtree(d)
