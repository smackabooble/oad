#!/bin/bash
cd /home/user/oad/web
URL="http://localhost:8098/?args=-autostart=skirmishes/acropolis_bay_2p%20-autostart-players=2%20-autostart-ai=2:petra"
for set in "move" "wheel,key" "click,drag" "right,click" ; do
  echo "=== $set" >> /tmp/bisect.log
  ACTIONS="$set" timeout 400 node stress.js "$URL" 240 2>&1 | grep -E "CRASHED|survived|out of bounds" | head -3 >> /tmp/bisect.log
done
echo DONE >> /tmp/bisect.log
