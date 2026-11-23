#!/bin/sh
set -eu
cd "$(dirname "$0")"
python3 generate-level.py
make -C engine -j2 lib
clang -std=c11 -D_POSIX_C_SOURCE=200809L -Iengine/src/data/include -Iengine/src/engine/include -Iengine/src/ui/include -Iengine/src/control/include -Iengine/src/control -O1 -g bridge.c -Lengine/build -l:libdm.dylib -lm -o bridge.next
