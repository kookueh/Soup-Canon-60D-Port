#!/bin/sh
# Regenerate every recipe stock from src/recipes/*.recipe into src/stocks
# and card/ML/FILM, and list them in both INDEX.TXT files after the
# original stocks. Safe to re-run.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
SRC=$(dirname "$HERE")
ROOT=$(dirname "$SRC")
for r in "$SRC"/recipes/*.recipe; do
    n=$(basename "$r" .recipe)
    python3 "$HERE/recipe2fs.py" "$r" -o "$SRC/stocks/$n.fs" --stocks "$SRC/stocks"
    cp "$SRC/stocks/$n.fs" "$ROOT/card/ML/FILM/$n.fs"
    for idx in "$SRC/stocks/INDEX.TXT" "$ROOT/card/ML/FILM/INDEX.TXT"; do
        grep -qx "$n.fs" "$idx" || printf '%s\n' "$n.fs" >> "$idx"
    done
done
