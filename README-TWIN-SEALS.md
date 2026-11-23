# Twin Seals — original-engine cooperative pilot

Start: PORT=18906 python3 server.py in coop-dungeon-pilot. Open http://127.0.0.1:18906 in two tabs and join different parties. WASD moves, QE turns; original mouse controls retained.
Party I touches seal (2,2), Party II touches seal (6,2). Owner-specific latches require both parties, remain latched after leaving or disconnecting. Both open native door (6,6). Meet below the door; I exits at (5,8), II at (7,8). One exit never wins.

Pipeline: edit level.json, run sh build.sh (max -j2). generate-level.py validates supported tiles, coordinates, overlaps, boundary, reachability and gate bypass and generates deterministic level.generated.h with canonical JSON hash. Native importer also checks map-slot capacity.
Runtime-authored map3 AFTER original recruitment, not serialized Dungeon.DAT. Original map3 geometry/thing lists entirely detached and replaced; old map-scoped pending events removed. A detached native door reused with button/force/magic flags zero. Other maps retained for recruitment only. Original Dungeon.DAT never written.

Controller scope: authoritative C bridge occupancy latches drive actual native door state; NOT original DAT sensor AND gate. Native movement/collision/graphics/inventory/formation/audio retained, eight original champions and supplied equipment. Cooperative snapshot fields are read-only; no debug/objective mutation endpoint. No extra encounter added, reliable puzzle/end prioritized.

Tests: python3 coop-test.py (determinism, five malformed negatives, actual two-owner HTTP native movement, first-seal negative, solo cannot activate second, shared native gate, one-exit negative, both-win). Inherited native-test 35/0, art-test.mjs254, controls-facing-test.mjs16; recompiled controls-native-test validates original swaps both parties/all headings. coop-browser-qa.cjs uses existing Playwright/Chromium. Evidence under evidence/.

Private original data remain local. No deployment, installs or public exposure. Memory-only progress resets at host restart. Linux Chromium is not Windows/Edge or two-physical-PC acceptance.
