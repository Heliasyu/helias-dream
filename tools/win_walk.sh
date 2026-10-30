#!/bin/bash
# World walk under wine: Play Offline, load, then hold the left stick; the probe role logs positions each second.
cd "$(dirname "$0")/.."
export WINEPREFIX=$PWD/build/win/wine WINEDEBUG=-all DISPLAY=:77 HELIAS_NO_GAMEPAD=1 HELIAS_SKIP_INTRO=1 HELIAS_NP_TEST=probe
rm -f build/frame-3900.ppm
HELIAS_AUTOPRESS="f1000:down,f1040:cross,f1150:cross,f1300:cross,f1450:cross,f1600:cross,f1800:cross,f2000:cross,f3300:lup:6000,f3600:lright:4000" HELIAS_DUMP_FRAME=3900 timeout -k 5 ${WALK_SECS:-200} tmp/win-run.sh > build/win/walk-win.log 2>&1
echo "EXIT=$?" >> build/win/walk-win.log
wineserver -k 2>/dev/null
echo WINE-DONE >> build/win/walk-win.log
