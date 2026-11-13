#!/bin/bash
# build everything: bsc (pinned), the codecs, the harnesses; then prove every integer law on its whole domain
set -e
cd "$(dirname "$0")"
INC="-I/opt/homebrew/include"; LIB="-L/opt/homebrew/lib"          # Homebrew on macOS; system paths elsewhere
if [ ! -d third_party/libbsc ]; then
  mkdir -p third_party && git clone -q https://github.com/IlyaGrebnov/libbsc.git third_party/libbsc
  (cd third_party/libbsc && git checkout -q 131f4403c54c66083a8e465a3a3179bd09dc871d)
fi
[ -f third_party/libbsc/build/libbsc.a ] || (cd third_party/libbsc && cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
   -DBSC_ENABLE_OPENMP=OFF -DBSC_ENABLE_CUDA=OFF -DBSC_ENABLE_NATIVE_COMPILATION=ON >/dev/null && cmake --build build -j4 >/dev/null)
CC="clang -std=c11 -O2 -fwrapv -w"; B="-Icodec -Ithird_party/libbsc third_party/libbsc/build/libbsc.a -lc++"
mkdir -p bin
$CC -o bin/shzx codec/shzx.c                                  # fast (c) and max (x) modes, one decoder (d)
$CC -o bin/shza codec/shza.c $B;  $CC -DSHZ_REF -o bin/shza_ref codec/shza.c $B
$CC -o bin/shzb codec/shzb.c $B;  $CC -DSHZ_REF -o bin/shzb_ref codec/shzb.c $B
$CC -DSHZ_NO_MAIN $INC -o bin/classic bench/classic.c codec/shz.c $LIB -lzstd -llzma -lbrotlienc -lbrotlidec -llz4 -lz -lbz2
$CC -o bin/bsc_bench bench/bsc_bench.c $B
$CC -o bin/ai_bench bench/ai_bench.c $B
$CC -o bin/aib_bench bench/aib_bench.c $B
for p in prove xprove aprove bprove; do $CC -o bin/$p proofs/$p.c; done
echo "built: $(ls bin | tr '\n' ' ')"
echo "proving every law on its whole domain (about a minute)..."
for p in prove xprove aprove bprove; do bin/$p | tail -1; done
