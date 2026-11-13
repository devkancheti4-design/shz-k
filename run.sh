#!/bin/bash
# re-run the sealed tests; writes results/*.csv, then report.md (priced with prices.json) and a size check vs expected/
#   ./run.sh ai | aiapi | logs | world | all
set -e
cd "$(dirname "$0")"; mkdir -p results; H="split,category,file,bytes,codec,comp_bytes,ctime_s,dtime_s,ok"
exact() { local ok=0 bad=0; for f in "$@"; do bin/$EXE c "$f" /tmp/r_l.z; bin/${EXE}_ref c "$f" /tmp/r_r.z; bin/$EXE d /tmp/r_l.z /tmp/r_o
          if cmp -s /tmp/r_l.z /tmp/r_r.z && cmp -s "$f" /tmp/r_o; then ok=$((ok+1)); else bad=$((bad+1)); echo "EXACT FAIL $f"; fi; done
          echo "exact ($EXE): $ok identical streams (laws = plain meanings) and byte-exact round trips, $bad failures"; }
ai() {
  F=(corpus/wildchat/sealed/*.jsonl)
  EXE=shza exact "${F[@]}"; EXE=shzb exact "${F[@]}"
  echo $H > results/ai_shza.csv;    bin/ai_bench sealed ai "${F[@]}" >> results/ai_shza.csv
  echo $H > results/ai_shzb.csv;    bin/aib_bench sealed ai "${F[@]}" >> results/ai_shzb.csv
  echo $H > results/ai_bsc.csv;     bin/bsc_bench sealed ai "${F[@]}" >> results/ai_bsc.csv
  echo $H > results/ai_classic.csv; bin/classic sealed ai "${F[@]}" >> results/ai_classic.csv
  python3 bench/zpaq_ref.py results/ai_zpaq.csv sealed ai "${F[@]}" || true
  python3 bench/parquet_ref.py results/ai_parquet.csv sealed ai "${F[@]}"; }
aiapi() {
  for kind in embeddings logprobs; do F=(corpus/aiapi/sealed/${kind}_*.jsonl)
    EXE=shzb exact "${F[@]}"
    echo $H > results/${kind}_shzb.csv;    bin/aib_bench sealed $kind "${F[@]}" >> results/${kind}_shzb.csv
    echo $H > results/${kind}_bsc.csv;     bin/bsc_bench sealed $kind "${F[@]}" >> results/${kind}_bsc.csv
    echo $H > results/${kind}_classic.csv; bin/classic sealed $kind "${F[@]}" >> results/${kind}_classic.csv
    python3 bench/zpaq_ref.py results/${kind}_zpaq.csv sealed $kind "${F[@]}" || true
    python3 bench/parquet_ref.py results/${kind}_parquet.csv sealed $kind "${F[@]}"; done; }
logs() {
  S=$(grep '^sealed:' corpus/loghub_split.txt | cut -d' ' -f2-)
  L=(); C=(); for s in $S; do L+=(corpus/loghub/${s}_2k.log); C+=(corpus/loghub/${s}_2k.log_structured.csv); done
  echo $H > results/loghub_classic.csv; bin/classic sealed logs "${L[@]}" >> results/loghub_classic.csv; bin/classic sealed csv "${C[@]}" >> results/loghub_classic.csv
  python3 bench/zpaq_ref.py results/loghub_zpaq.csv sealed logs "${L[@]}" || true; python3 bench/zpaq_ref.py results/loghub_zpaq.csv sealed csv "${C[@]}" || true; }
world() {
  for f in data/enwik8/enwik8 data/silesia/*; do bin/shzx t "$f" | head -1; bin/shzx tx "$f" | head -1; done | tee results/world_sizes.txt; }
case "$1" in ai) ai ;; aiapi) aiapi ;; logs) logs ;; world) world ;; all) ai; aiapi; logs ;; *) echo "usage: ./run.sh ai|aiapi|logs|world|all"; exit 2 ;; esac
python3 report.py results > /dev/null && echo "wrote report.md"
python3 verify.py
