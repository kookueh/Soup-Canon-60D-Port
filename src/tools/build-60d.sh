#!/bin/sh
# Build soup.mo for the EOS 60D (firmware 1.1.1).
#
#   tools/build-60d.sh /path/to/magiclantern_simplified
#
# Copies integrations/magiclantern/modules/soup into the Magic Lantern tree
# as modules/soup, builds Magic Lantern for 60D.111 (needed for the symbol
# list modules are checked against), builds the module, then checks every
# symbol it imports is exported by the 60D build.
#
# Output: ../card60d/ laid out as the card root (ML/modules/soup.mo,
# ML/FILM/*). Magic Lantern itself comes from the official 60D build.
set -eu

ML=${1:?usage: build-60d.sh /path/to/magiclantern_simplified}
HERE=$(cd "$(dirname "$0")" && pwd)
SRC=$(dirname "$HERE")
ROOT=$(dirname "$SRC")
MOD="$ROOT/integrations/magiclantern/modules/soup"
OUT="$ROOT/card60d"

rm -rf "$ML/modules/soup"
cp -r "$MOD" "$ML/modules/soup"

make -s -C "$ML/platform/60D.111" >/dev/null
make -s -C "$ML/modules/soup" SOUP_ENGINE="$SRC/engine" clean >/dev/null
make -C "$ML/modules/soup" SOUP_ENGINE="$SRC/engine"

SYM="$ML/platform/60D.111/build/magiclantern.sym"
DEP="$ML/modules/soup/build/soup.dep"
missing=0
for s in $(cat "$DEP"); do
    if ! grep -q " $s\$" "$SYM"; then
        echo "MISSING on 60D: $s"; missing=1
    fi
done
[ $missing -eq 0 ] || { echo "soup.mo imports symbols the 60D build does not export"; exit 1; }
echo "all $(wc -l < "$DEP") imported symbols are exported by 60D.111"

rm -rf "$OUT"
mkdir -p "$OUT/ML/modules" "$OUT/ML/FILM"
cp "$ML/modules/soup/build/soup.mo" "$OUT/ML/modules/"
cp "$ROOT/card/ML/FILM/"* "$OUT/ML/FILM/"
ls -l "$OUT/ML/modules/soup.mo"
