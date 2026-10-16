#!/bin/bash
# rebuild A27 engine + debug link into www27 + browser run. usage: iter27.sh [secs]
SECS=${1:-40}
source /home/user/0ad-web/emsdk/emsdk_env.sh >/dev/null 2>&1
export OAD_SRCDIR=src27 OAD_BUILD=/home/user/0ad-web/build_a27
cd $OAD_BUILD || exit 1
ninja -j4 > ninja.log 2>&1 || { echo BUILD FAILED; grep -E "error" -A3 ninja.log | head -30 | cut -c1-250; exit 1; }
./link.sh /home/user/0ad-web/www27/pyrogenesis.js -sWASMFS -sPROXY_TO_PTHREAD -sOFFSCREENCANVAS_SUPPORT "-sOFFSCREENCANVASES_TO_PTHREAD=#canvas" -sENVIRONMENT=web,worker --profiling-funcs -sASSERTIONS=1 -sJSPI=1 2> link_dbg.log || { echo LINK FAILED; head -5 link_dbg.log; exit 1; }
cd /home/user/oad/web
timeout $((SECS+120)) node test_browser.js "http://localhost:8098/?debug" $SECS /tmp/shot27.png 2>&1 | grep -v "^\[worker\]\|AudioContext was not" | cut -c1-300 | head -${LINES_MAX:-40}
