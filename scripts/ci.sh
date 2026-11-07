#!/bin/sh
# ci.sh <Data/Models dir> <Usagi engine dir> [golden hash file]
#
# Builds fbx-to-vmdf, runs the self-test, converts every model under the
# given directory the way the engine's rake 'cmdl' rule does, and compares
# the SHA-256 of every output (.vmdf and skeleton .xml) with a golden list
# made on Linux. Any host that prints "hashes: all N match" produces
# byte-identical output. Works with Git Bash on Windows and on macOS.
#
# Environment: BUILD_DIR (default build-ci), CMAKE_ARGS (extra configure
# arguments, e.g. -G "Visual Studio 17 2022" -A x64), CONFIG (Release).
set -eu

MODELS=${1:?usage: ci.sh <Data/Models dir> <Usagi engine dir> [golden]}
USAGI=${2:?usage: ci.sh <Data/Models dir> <Usagi engine dir> [golden]}
HERE=$(cd "$(dirname "$0")/.." && pwd)
GOLDEN=${3:-$HERE/tests/golden/usagigp-models.sha256}
BUILD_DIR=${BUILD_DIR:-$HERE/build-ci}
CONFIG=${CONFIG:-Release}

# shellcheck disable=SC2086
cmake -S "$HERE" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE="$CONFIG" ${CMAKE_ARGS:-}
cmake --build "$BUILD_DIR" --config "$CONFIG" -j 4
ctest --test-dir "$BUILD_DIR" -C "$CONFIG" --output-on-failure

EXE=$BUILD_DIR/fbx-to-vmdf
[ -x "$EXE" ] || EXE=$BUILD_DIR/$CONFIG/fbx-to-vmdf.exe
[ -x "$EXE" ] || { echo "ci: no fbx-to-vmdf binary in $BUILD_DIR" >&2; exit 1; }

if command -v sha256sum >/dev/null 2>&1; then SHA="sha256sum"; else SHA="shasum -a 256"; fi

OUT=$BUILD_DIR/ci-out
rm -rf "$OUT"
mkdir -p "$OUT"
export USAGI_DIR="$USAGI"
fail=0
( cd "$MODELS" && find . -name '*.fbx' | grep -v /collision | sort ) | while read -r f; do
  sub=${f#./}; sub=${sub%.fbx}
  o=$OUT/Models/$sub.vmdf
  mkdir -p "$(dirname "$o")" "$OUT/skel/$(dirname "$sub")"
  if ! "$EXE" --quiet -a16 "-o$o" "-sk$(dirname "$o")/" -lh "-d$o.d" "-h$OUT/skel/$sub.vmdf.xml" "$MODELS/$sub.fbx" > "$o.log" 2>&1; then
    echo "ci: FAILED $sub (see $o.log)"
  fi
done

( cd "$OUT" && find Models skel -type f \( -name '*.vmdf' -o -name '*.xml' \) | sort | while read -r p; do $SHA "$p"; done ) | sed 's/ \*/  /' > "$OUT/hashes.sha256"
if [ ! -f "$GOLDEN" ]; then
  echo "ci: no golden list at $GOLDEN; this run's hashes are in $OUT/hashes.sha256"
  exit 0
fi
total=$(wc -l < "$GOLDEN" | tr -d ' ')
if diff "$GOLDEN" "$OUT/hashes.sha256" > "$OUT/hashes.diff"; then
  echo "hashes: all $total match $GOLDEN"
else
  n=$(grep -c '^[<>]' "$OUT/hashes.diff" || true)
  echo "hashes: MISMATCH ($n differing lines, see $OUT/hashes.diff)"
  fail=1
fi
exit $fail
