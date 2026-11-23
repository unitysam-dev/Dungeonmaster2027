# Isolated original-engine sound candidate

No live host/proxy/tunnel changed. No restart of the current game. Parent approval/review and Sam approval for progress-losing activation remain required.

## Run
From this directory: sh build.sh; PORT=18890 sh start-linux.sh. Loopback only. No candidate host is intentionally left running after tests. Linux host/Chromium tested; not Windows or human listening acceptance.

## Exact functional changes from level4-native-browser
- engine/src/engine/include/sound.h: world-source hook with original identity/map/square.
- engine/src/engine/sound.c: ignore NONE; schedule EVENT on source map and emit only when original due event dispatch calls sound_request_play; hook precedes active listener map/range filtering. Ordinary NOW/AFTER_TICK requests emit once; native playback/pending remains independent.
- bridge.c: 256-event bounded monotonic journal, time/id/map/x/y/sound and soundCursor in snapshots. Hook installed after fixture initialization. Rendering never fabricates events.
- server.py: default isolated port18890 and explicit /audio.js /original-sounds.json static routes.
- client.js: original native world adapter with per-party facing/map/connectivity, gesture Enable sound and visible load/unlock/playback errors; rejoin baseline.
- audio.js: retained v10 signed8/Paula/3-pole renderer unchanged, event baseline/dedup/map/offline filtering added. Original 35-definition manifest copied byte-for-byte from v10 private-assets; NOT a new extraction/decode verification. Sound samples only local/private.
- test support: sound-native-test.c, sound-qa.cjs, waveform-test.mjs; inherited http-test.py port18891 and browser-qa.cjs port18892. inspect-level.c retained for test.sh. implement.py is historical, non-idempotent: DO NOT RERUN.

## Reproduce
sh test.sh: build + inherited 35 native and 19 HTTP regressions; evidence/regressions.log.
Compile sound-native-test.c with same includes/link flags as build.sh (replace bridge.c/output), then LD_LIBRARY_PATH=engine/build ./sound-native-test private-game > evidence/sound-native.log.
node waveform-test.mjs: 35 retained payload hashes/periods, independent sample8 signed8 clock/filter/stereo recurrence tolerance1e-7, native table/facing comparisons.
For browser QA use LD_LIBRARY_PATH and FONTCONFIG_FILE from browser-test.sh, then node sound-qa.cjs. It spawns/stops candidate18890, uses two pages in SAME context, clicks Enable sound on both, clicks original torch hand then original throw region on A. Native sound13 swing and sound0 automatic metallic impact at B square appear in both journals and both analyser traces (>1e-5). No fake own-command audio callbacks. JSON traces/screenshots and log are under evidence/sound-browser-*.
node browser-qa.cjs reruns inherited 24 browser regression checks on18892. Separate door/security tests not rerun in this bounded pass.

## Parent activation integration
DO NOT point the live gate at this candidate without approval: restarting native world loses progress/tokens. Existing authenticated gate allowlist must add exact GET /audio.js and /original-sounds.json alongside existing assets, with the SAME authentication requirement. Keep original-sounds.json private; never expose direct public host. Candidate static server has baseline loopback/Host protections but asset GETs assume the eventual authenticated outer gate, as original frame client assets did.

## Limits
Journal captures original request emissions before active-listener filter, rather than preserving original two-slot priority arbitration independently per listener; browser retains v10 two-voice FIFO replacement. Thus sample identity, source timing and spatial gains are grounded, but exact simultaneous channel arbitration/continuous filter-state mixer parity is NOT claimed. Poll cadence may add ~350ms latency and bursts >256 events can drop oldest events. No wall occlusion beyond original range/table attenuation. No mute toggle (Enable only). UI audio error text may be overwritten by subsequent status updates. No new assets/source downloads/install/config changes/public uploads. Native explicit sound_request tests establish delayed/NONE/range behavior; browser original throw establishes actual gameplay swing+automatic impact, not a separate live sword attack/door sound acceptance. Human/Windows listening still pending.
