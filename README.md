# Dungeon Master 2027

Deep Circuit Menagerie is an experimental two-party Dungeon Master multiplayer prototype. It uses a native shared-world engine and a Python server with browser clients. This is a source snapshot, not a game download or a deployment.

## Build and run on Linux

Install Python 3.11 or later, Make, and Clang. Build with `sh build.sh`. Point `DM_DATA` at your own lawful Amiga 3.60 game data (Dungeon.DAT, Graphics.DAT, KAOS.FTL, and optionally VDEO.FTL), then run:

```sh
DM_DATA=/absolute/path/to/your/game-data sh start-linux.sh
```

Open the loopback URL printed by the server. Choose different parties in two browser tabs. The server is intended for local testing; do not expose it directly to the Internet. This snapshot does not certify the reported formation issue on Edge.

The original game files and extracted audio samples are intentionally not included. Sound requires a compatible locally supplied `original-sounds.json`; the browser and server sound code is included. Build products, saved games, deployment state and credentials are not included.

## Source and license

The modified native engine is based on [DMMac](https://github.com/peterkmet/dmmac). Its original source notices and GPL-3.0 license are retained under `engine/`. This repository does not license or distribute the original game data.
