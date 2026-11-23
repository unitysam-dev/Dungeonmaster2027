#!/bin/sh
set -eu
cd "$(dirname "$0")"
mkdir -p evidence
sh build.sh > evidence/build-current.log 2>&1
for name in native-test inspect-level; do
 clang -std=c11 -D_POSIX_C_SOURCE=200809L -Iengine/src/data/include -Iengine/src/engine/include -Iengine/src/ui/include -Iengine/src/control/include -Iengine/src/control -O1 -g "$name.c" -Lengine/build -l:libdm.dylib -lm -o "$name"
done
LD_LIBRARY_PATH=engine/build ./native-test "${DM_DATA:-private-game}" > evidence/native-final.txt
cat evidence/native-final.txt
LD_LIBRARY_PATH=engine/build ./inspect-level "${DM_DATA:-private-game}" > evidence/level-original.txt
python3 http-test.py > evidence/http-current.txt
cat evidence/http-current.txt
node --check client.js
node --check party-marker.js
python3 -m py_compile server.py
