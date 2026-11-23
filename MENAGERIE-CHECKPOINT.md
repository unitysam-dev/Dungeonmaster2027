# Deep Circuit: Menagerie
Separate local candidate. No deployment, live reset, new network exposure, or original game data change. Game runtime remains two original-engine parties, existing eye fix, shared co-op gate, shortcuts and existing supplies.

The 28x28 layout retains 16 chambers with one clear starting room and one native group of one creature in each other room. All 15 types rendered in real native framebuffers (positive fixture, both party contexts) and the roster survived 100 shared engine ticks. Original AI/graphics were retained. The renderer uses a shared floor palette, so some creature colors can differ from their original floors. Balance and full campaign completion not tested.

Native west/east door switches at (3,6) and (9,6); face south from (3,5) and (9,5). Two independent Chromium mobile contexts navigated from actual starts (no fixture teleports), fitted 390x844 and 768x1024, tapped the original switches, and each door opened and closed for both players. See evidence/phone-doors.log and door-*.png. Not confirmed on a physical phone.

Gameplay map: level.json -> generate-level.py -> bridge runtime import -> original group scheduler and view renderer; record.c provides a scoped in-memory roster keeping original ornaments. Switch path is canvas pointer conversion -> native mouse press/release -> original door command -> timeline animation -> shared door state. Contextual HUD is read-only. Earlier fireball report is not a change in this build.

If approved to replace the live preview, back up changed runtime files, deploy with game-only restart (both parties reset), recheck public access before handoff. Runtime/source changes: bridge, bridge.c, level.json, level.generated.h, generate-level.py, client.js, index.html, engine/src/engine/record.c, engine/src/engine/include/record.h, engine/build/libdm.dylib. The compiled candidate and old build remain separate.
