#!/bin/bash
# Build + link the release and assemble a distributable folder.  usage: make_dist.sh [OUT_DIR]
set -e
ROOT=${OAD_ROOT:-/home/user/0ad-web}; REPO=${OAD_REPO:-/home/user/oad}
OUT=${1:-$ROOT/dist}
source $ROOT/emsdk/emsdk_env.sh >/dev/null 2>&1
python3 $REPO/build/gen_ninja.py > /dev/null
cd ${OAD_BUILD:-$ROOT/build_mt} && ninja -j4
mkdir -p "$OUT/data_extra/icu"
./link.sh "$OUT/pyrogenesis.js" -sWASMFS -sPROXY_TO_PTHREAD -sOFFSCREENCANVAS_SUPPORT "-sOFFSCREENCANVASES_TO_PTHREAD=#canvas" \
  -sENVIRONMENT=web,worker -sJSPI=1 -sASSERTIONS=0
cp $REPO/web/index.html $REPO/web/serve.py "$OUT/"
cp $REPO/web/README_WEB.md "$OUT/README.md"
cp $ROOT/webdata/data/icu/icudt75l.dat "$OUT/data_extra/icu/"
ls -la "$OUT"
