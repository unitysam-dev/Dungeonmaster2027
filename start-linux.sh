#!/bin/sh
set -eu
cd "$(dirname "$0")"
if [ ! -f ./bridge ] || [ ! -f engine/build/libdm.dylib ]; then sh build.sh; fi
chmod u+x ./bridge
exec python3 server.py
