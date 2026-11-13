#!/bin/bash
# fetch the public data and check every file against the published hashes
#   ./fetch_data.sh ai        WildChat-1M shard 0 (230.8 MB, ODC-BY) -> 14 batches of 10 MB; the AI-API batches (shipped)
#   ./fetch_data.sh logs      Loghub 2k logs and their CSVs (16 systems, about 11 MB, from github.com/logpai/loghub)
#   ./fetch_data.sh world     enwik8 (36 MB zip), Silesia (68 MB zip); enwik9 (323 MB zip) with ./fetch_data.sh enwik9
set -e
cd "$(dirname "$0")"; mkdir -p data corpus
check() { (cd "$1" && shasum -a 256 -c --quiet "$2") && echo "  hashes OK: $1"; }
case "$1" in
ai)
  mkdir -p data/wildchat
  [ -f corpus/wildchat/sealed/wildchat_00.jsonl ] || {
    curl -fL -o data/wildchat/shard0.parquet "https://huggingface.co/datasets/allenai/WildChat-1M/resolve/main/data/train-00000-of-00014.parquet"
    python3 -I corpus/build_wildchat.py data/wildchat/shard0.parquet && rm data/wildchat/shard0.parquet; }
  awk -F'\t' 'NR>1 {print $4"  "$1"/"$2}' corpus/WILDCHAT_MANIFEST.tsv > /tmp/wc.sha && check corpus/wildchat /tmp/wc.sha
  mkdir -p corpus/aiapi/sealed
  for f in data/aiapi/*.xz; do xz -dc "$f" > corpus/aiapi/sealed/$(basename "$f" .xz); done
  awk -F'\t' '$1=="sealed" {print $4"  sealed/"$2}' corpus/AIAPI_MANIFEST.tsv > /tmp/aa.sha && check corpus/aiapi /tmp/aa.sha ;;
logs)
  mkdir -p corpus/loghub
  for s in Android Apache BGL HDFS HPC Hadoop HealthApp Linux Mac OpenSSH OpenStack Proxifier Spark Thunderbird Windows Zookeeper; do
    for suf in _2k.log _2k.log_structured.csv; do [ -f corpus/loghub/$s$suf ] || curl -fsSL -o corpus/loghub/$s$suf "https://raw.githubusercontent.com/logpai/loghub/master/$s/$s$suf"; done
  done
  check corpus/loghub ../LOGHUB_SHA256.txt ;;
world)
  mkdir -p data/enwik8 data/silesia
  [ -f data/enwik8/enwik8 ] || (cd data/enwik8 && curl -fsSL -o e.zip https://mattmahoney.net/dc/enwik8.zip && unzip -q e.zip && rm e.zip)
  [ -f data/silesia/dickens ] || (cd data/silesia && curl -fsSL -o s.zip https://sun.aei.polsl.pl/~sdeor/corpus/silesia.zip && unzip -q s.zip && rm s.zip)
  echo "a1fa5ffddb56f4953e226637dabbb36a  data/enwik8/enwik8" | md5sum -c 2>/dev/null || [ "$(md5 -q data/enwik8/enwik8)" = a1fa5ffddb56f4953e226637dabbb36a ] && echo "  enwik8 md5 OK" ;;
enwik9)
  mkdir -p data/enwik9; [ -f data/enwik9/enwik9 ] || (cd data/enwik9 && curl -fsSL -o e.zip https://mattmahoney.net/dc/enwik9.zip && unzip -q e.zip && rm e.zip)
  [ "$(md5 -q data/enwik9/enwik9 2>/dev/null || md5sum data/enwik9/enwik9 | cut -d' ' -f1)" = e206c3450ac99950df65bf70ef61a12d ] && echo "  enwik9 md5 OK" ;;
*) echo "usage: ./fetch_data.sh ai | logs | world | enwik9"; exit 2 ;;
esac
