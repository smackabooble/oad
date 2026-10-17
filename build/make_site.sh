#!/bin/bash
# Assemble the GitHub Pages bundle in docs/ from a built dist/.  usage: make_site.sh [DIST] [OUT]
set -e
REPO=${OAD_REPO:-/home/user/oad}; DIST=${1:-$REPO/dist}; OUT=${2:-$REPO/docs}
mkdir -p "$OUT/data_extra/icu"
cp "$DIST"/pyrogenesis.js "$DIST"/pyrogenesis.wasm "$OUT/"
cp "$DIST"/pyrogenesis.*.js "$OUT/" 2>/dev/null || true
cp "$DIST/data_extra/icu/icudt75l.dat" "$OUT/data_extra/icu/"
cp $REPO/site/index.html $REPO/site/sw.js $REPO/site/dl-worker.js $REPO/site/net.js $REPO/site/peerjs.min.js "$OUT/"
touch "$OUT/.nojekyll"
ls -la "$OUT"
