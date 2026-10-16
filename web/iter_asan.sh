#!/bin/bash
# rebuild ASAN engine, relink into www27a, run the stress test.  usage: iter_asan.sh [secs]
source /home/user/0ad-web/emsdk/emsdk_env.sh >/dev/null 2>&1
cd /home/user/0ad-web/build_asan || exit 1
ninja -j4 > ninja.log 2>&1 || { echo BUILD FAILED; grep error -A3 ninja.log | head -20; exit 1; }
./link.sh /home/user/0ad-web/www27a/pyrogenesis.js -sWASMFS -sPROXY_TO_PTHREAD -sOFFSCREENCANVAS_SUPPORT "-sOFFSCREENCANVASES_TO_PTHREAD=#canvas" -sENVIRONMENT=web,worker -sASSERTIONS=1 -sJSPI=1 -sINITIAL_MEMORY=1GB -sMAXIMUM_MEMORY=4GB 2> link_a.log || { echo LINK FAILED; tail -3 link_a.log; exit 1; }
cd /home/user/oad/web
(curl -s -o /dev/null localhost:8101/ ) || (cd /home/user/0ad-web/www27a && setsid nohup python3 serve.py --data /home/user/0ad-web/webdata27/data --port 8101 > /tmp/serve101.log 2>&1 < /dev/null &) ; sleep 2
timeout $((${1:-300}+300)) node stress.js "http://localhost:8101/?args=-autostart=skirmishes/acropolis_bay_2p%20-autostart-players=2%20-autostart-ai=2:petra" ${1:-300} > /tmp/asan.log 2>&1
grep -n -m1 "Sanitizer" /tmp/asan.log; head -${LINES_MAX:-40} /tmp/asan.log | cut -c1-250
