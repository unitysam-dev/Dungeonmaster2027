# Continue development

The current candidate is Deep Circuit Menagerie. See [the checkpoint](MENAGERIE-CHECKPOINT.md) for the map and known limits. This is not the deployed game.

- `server.py`: local HTTP host and shared game state. `bridge.c`: native engine adapter and party contexts.
- `level.json` → `generate-level.py` → `level.generated.h`: map source and generated native data. Edit the JSON, not the generated header.
- `client.js`, `index.html`, `party-art.js`, `audio.js`: browser UI, rendering extensions and sound playback.
- `engine/`: modified GPL DMMac source. See its license and docs.

Build as described in the README. After building, run `DM_DATA=/absolute/path/to/game python3 smoke-test.py` for the current map. The inherited `test.sh` is retained for reference: its native checks currently report 32 passes and three failures on the old door fixture, so it stops before the legacy HTTP tests. Browser regression scripts use the npm `playwright` package and its installed Chromium. Run `npm install --no-save playwright`, then `npx playwright install chromium`. After building, `mkdir -p evidence` and run `DM_DATA=/absolute/path/to/game node evidence/phone-doors.cjs`. Some older browser harnesses also need fixture coordinates in `evidence/mouse-targets.txt` and/or the optional sound samples; they aren't a complete automated suite for this version.

Open issue: reproduce formation selection and dragging in real desktop Edge before claiming it fixed. The `pc-formation-repro.cjs` and `touch-formation-repro.cjs` scripts are starting points; their Chromium results do not establish Edge behavior.
