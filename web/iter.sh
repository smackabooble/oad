#!/bin/bash
# rebuild + relink + browser smoke test. usage: iter.sh [seconds] [url-args]
SECS=${1:-60}; ARGS=${2:-}
source /home/user/0ad-web/emsdk/emsdk_env.sh >/dev/null 2>&1
cd /home/user/0ad-web/build_mt || exit 1
ninja -j4 > ninja_iter.log 2>&1 || { echo "BUILD FAILED"; grep -E "error" -A3 ninja_iter.log | head -30 | cut -c1-250; exit 1; }
./link.sh /home/user/0ad-web/www/pyrogenesis.js -sWASMFS -sPROXY_TO_PTHREAD -sOFFSCREENCANVAS_SUPPORT "-sOFFSCREENCANVASES_TO_PTHREAD=#canvas" -sENVIRONMENT=web,worker --profiling-funcs -sASSERTIONS=1 --pre-js /home/user/oad/web/pre.js ${ASYNC_FLAGS:--sJSPI=1} 2> link_web.log || { echo "LINK FAILED"; grep -E "error" link_web.log | head -10 | cut -c1-250; exit 1; }
curl -s -o /dev/null http://localhost:8080/index.html || { (cd /home/user/0ad-web/www && nohup python3 /home/user/oad/web/server.py 8080 . > ../server.log 2>&1 &); sleep 2; }
cd /home/user/oad/web
timeout $((SECS+60)) node test_browser.js "http://localhost:8080/index.html${ARGS:+?args=$ARGS}" $SECS /tmp/shot.png 2>&1 | grep -v "^\[worker\]" | cut -c1-420 | head -${LINES_MAX:-60}
