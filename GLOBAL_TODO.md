# Global todo: FM4 Evolution (updated 2026-10-12 AEDT)

## Project rules (read first; full list in the claude.ai project doc `claude/00-project-rules-read-first.md`)
- The base `gamedb.slt` is overridden by the Damage Plus pack (offer 170, `DmgDB_pri_27100.zip` at `game:\Media\db\`). DLC merges are `INSERT OR IGNORE` (add rows only). Change existing base rows only through `tools/dlc/make_damage_pack.py --db-sql` and rebuild; never edit the loose `gamedb.slt`, never add a second `game:\Media\db\` override.
- Every car must have damage: after installing or rebuilding any car pack, rebuild and reinstall Damage Plus (it overrides every car's carattribs.xml and fills missing morph data with `tools/dlc/fill_morph.py`).


One list across all the work in this project: gameplay features, car ports from FH, FH2, FH3, FM2 and FM3, the renderer, and housekeeping. Detail lives in the linked docs. The repo copy is `GLOBAL_TODO.md`; local copies of the main docs are in `C:\Z-Temp\Forza-4-compiling-stuff\_claude_stage\project-docs\` (outside the repo, so a recomp rebuild cannot remove them). Brakes are deliberately deferred.

## Installed now (see the id table in `claude/car-port-handoff.md`)
- **FH DLC:** offers 71 to 81 (Honda pack races; the others load in menus, not raced). Offer 70 (FH Shelby GT500) was an old test mule and is retired (user, 2026-10-11); backup only, not a reference.
- **FH2:** LaFerrari (offer 82, car 1756) loads and starts a race after the dangling-reference fix.
- **FM3:** Aveo5 LT (offer 85) loads and drives; brakes not visible. Batch 1 (offer 84, 11 cars): Boxster S and Monte Carlo SS confirmed, 9 untested.
- **FM2:** Lotus Elise 135R (offer 86, car 1770) works.
- **FH3:** offer 101 = car 9330 "Holden Torana A9X (FH3)", clean port with the fixed writer, its own FH3 database rows, the real FH3 paint list and a damage-morph test (v8). Awaiting test. See `claude/fh3-torana-clean-port-2026-10-11.md`. Offers 99 and 100 (Monaro, Mustang tests) removed from the live install.
- **GT6 wheels:** offer 238 (`4D530910F4FFEE00`, package `C3997BF5480EF26541ABDFA30D23559E709641AF4D`), 3 Volk rims (57XV, 57D, TE37V; wheel ids 12900-12902) under their own maker tile 1710 "Gran Turismo 6". 57D fits perfectly in game; 57XV/TE37V reworked (v4, aligned to the 57D), being tested by others. Recipe: skill `gt6-wheel-import`; see `claude/knowledgebase_addendum_gt6_wheels_2026-10-18.md`.
- **Real AI drivers:** offer 130 (package `74ED6772D7DC8E2AC3230FB1E3B56EFE02B9B79D4D`), works with the `AiDriverPoolQuery` hook.

## Gameplay features (code hooks in `fm4/fm4_hooks.toml` + `fm4/src/hooks.cpp` / `dev_keys.cpp`; each has an env var to switch it off)
| Feature | Status | Switch | Doc |
|---|---|---|---|
| Event List scrolling (more than 310 events) | Working (user) | `FM4_EVENT_GRID_SCROLL` | `claude/event-list-grid-ui-2026-10-12.md` |
| Developer keys (F6 credits, F8 level, F9/Shift+F9 win, F10 reset, F11 affinity) | Working (user); live UI refresh after F8/F11 awaiting test | `FM4_DEV_KEYS` | `claude/dev-keys-2026-10-12.md` |
| Prize car for completing an event (choose-a-car popup) | Working: offered after an event completes (seen via F9) | `FM4_PRIZE_CARS` | `claude/prize-car-rewards-2026-10-12.md` |
| **Real AI drivers** (471 real drivers with real countries and flags, random pick per region and rank; offer 130 + `AiDriverPoolQuery` hook) | **Success (user, 2026-10-12)** | `FM4_AI_DRIVER_POOL` | `claude/real-ai-drivers-2026-10-12.md` |
| Upgrade-dependent engine audio (intake/exhaust level slots) | VW Fox test works (user); other cars need per-level sound data | `FM4_UPGRADE_AUDIO` | `claude/upgrade-audio-framework-2026-10-12.md` |
| Used Car Dealership | Feasibility only, no code | | `claude/used-car-dealership-feasibility-2026-10-12.md` |
| Mod manager | Feasibility only, no code | | `claude/mod-manager-feasibility-2026-10-12.md` |
| Main menu OPTIONS & MODS tile (submenu: MODS overlay = read-only pack explorer + feature list; Assists, Controller, Audio, Credits) | Applied 2026-10-13, awaiting full rebuild + test | `FM4_MODS_MENU` | `claude/main-menu-mods-tile-research-2026-10-13.md` |

- [ ] Dynamic AI (2026-10-13): rebuild + test in game. Rubber-banding off and driver personalities are in the real-drivers pack (offer 130, reinstalled); hooks AiFormSkill/AiFormTemper (race-day form) and AiRandomUpgradeTarget (random AI upgrade level under the PI limit) in fm4/src/hooks.cpp. Toggles FM4_AI_FORM=0, FM4_AI_RANDOM_UPGRADES=0. See claude/dynamic-ai-2026-10-13.md.
- [ ] AI drivers follow-ups (optional): bios are English in every language; pack has no name in the DLC list; free-play pool (DriverTypeID 3) is still 73 drivers (renamed, not enlarged). Edit `tools/dlc/ai_drivers_data.py` and rerun `make_ai_drivers_pack.py --install` to change the roster.

## Timeline (milestones, AEDT)
- **2026-10-08:** v0.1.0-alpha.1 source release prepared (builder-first path, docs, hero artwork).
- **2026-10-09:** FM4 post-launch DLC support; first FH DLC car packs ported (offers 71 to 81).
- **2026-10-10:** First FM2 car working in FM4 (Lotus Elise 135R, offer 86). FM3 Aveo5 LT drives. FH3 model and texture formats decoded.
- **2026-10-11:** FH3 Torana A9X clean port loads and races (v7); damage morphs decoded and working (v8 to v10).
- **2026-10-12:** Torana v16 confirmed (grille, decals, livery masks, cameras, dials). Event List scrolling, developer keys and event prize cars working. Upgrade audio test (VW Fox) works. **Real AI drivers: success.**

## Key findings (2026-10-11), apply to every converter
- FM4 car vertices store baked per-vertex ambient lighting in words 12-15 (order-1 SH) and use two UV sets (set 1 = per-car atlas, set 2 = tiling). Zero lighting values = matte black body. See `claude/converter-writer-test-step1-2026-10-11.md`.
- FM4 damage deformation lives in each exterior submodel's Morph / Morph_LOD0 buffer (4 bytes per vertex, k_10_11_11 direction + magnitude, MorphTargets flag 1). Our writers leave it empty, so ported cars show no damage. Format and FH3 conversion: Torana doc (v9).

## Open: FH3 pipeline
- [x] v7 Torana tested (user): loads and races, model decent; only yellow paint, no damage, brake lights are dots, some lighting fixes needed.
- [x] v8 Torana: damage works in game (user, 2026-10-11; deformation looks basic, as expected from the W427 transfer). Morph buffers confirmed as the damage mechanism.
- [x] 7 factory colours show and work (user, 2026-10-11).
- [x] FM4 damage morph decoded from the car vertex shader (k_10_11_11: direction x/z plus signed magnitude; scaled at runtime by the physics damage lattice). Torana v9 uses its own FH3 damage data (`fh3_morph.py`, `fm4morph.py`).
- [x] v9 damage deformation looks decent (user). Damaged areas went flat grey: body UV1 texel sat in the W427 damage-atlas scratch unwrap.
- [x] v10 tested OK (user).
- [x] v10 works (user): damaged areas keep the paint colour.
- [x] v12 panel gaps: works (user).
- [x] v13 tested (user): needles OK, damage works.
- [ ] Test v17 (installed 2026-10-12): dials read true, needles glow at night, mirrors reflect, scratches on damage, no shimmer, black interior, own thumbnail, Falcon 351 sound. Rollback: `roundtrip\backup_v17pre\`.
- [ ] Generic FH3 porter: fold the Torana pipeline (geometry, bake, morphs, shell, DB with cameras/track fixes, atlases/decals/dials, mirrors/needles, livery masks, thumbnail, audio pick) into one per-car tool, then test it on a second FH3 car (Monaro is the obvious one). Car-specific inputs: part-to-submodel map, a donor/reference FM4 car, cockpit reference car, audio donor.
- [x] v16 installed 2026-10-12 and confirmed a success by the user (grille, decals, livery masks, cockpit door area; on top of v15 cameras, wheel track, dials). Rollback: `roundtrip\backup_v16pre\`. User: race cockpit shows "a huge gap" at the doors (geometry test says the door is solid; clarify). White mirror faces in the cockpit.
- [ ] Torana remaining: speedo/tach tick calibration to the Torana's own dials (0-200 km/h, 0-7000 rpm); needles on `gauge_needle` (night glow); mirrors render white; xds mip chain; damage scratch marks; interior atlas still W427 colours; W427 leather/plastic normal maps and digital-dash leftovers; audio (Mustang-based); dealer thumbnail; FH3 race/upgrade body parts (race bumper, wing, hoods) as FM4 upgrades; bake brightness recalibration; damage strength tuning; porter fixes (cameras, wheel track) back into port_fh3_db.py.
- [ ] Test v14 (installed): black stepped patches around the flares/scoop should be gone (bake now starts rays 2.5 cm out). Rollback: `roundtrip\backup_v14pre\`.
- [ ] Torana: dial textures missing on the gauge faces (use the FH3 `hol_torana_77_gauges_diff/_lite` swatchbins); cockpit and bonnet camera views off; check wheel offset vs FH3 hubs/flares; bake slightly brighter than native after the tmin change (recalibrate later).
- [ ] (done in v13) Test v13: steering pivot and hands, driver pose, speedo/tach needles, exhaust smoke at the tips, mirror, 14-inch wheel and grey brakes, rear bumper/exhaust can drop off. Rollback: `roundtrip\backup_v13pre\`. Follow-ups: needle meshes don't use `gauge_needle` (no night glow); Torana's own gauge sweeps (FH3 needle animations); remove `digital_gauge` + W427 digital dash files as a separate test.
- [x] (in v13) Cockpit donor (user idea, 2026-10-12): swap the W427 (2009, digital dash) for an era-matched analog donor, proposed FOR_XBFalconGT_73. Take its `carattribs.xml` (speed/tach tick calibration, driver rig offsets, Damage section incl. PartDropValues: Falcon drops bumperR and exhausts, W427 only mirrors/wing), drop the W427 `digitalgauge/` files, re-fit the shell's butt/hand points to the Falcon rig. Later: Torana's own speedo/tach scale from its FH3 gauge texture and needle animations.
- [x] (v13) Merge the staged agent work into one pack after v12: own shell (`_claude_stage\shell\`, root/Model16 patch after `fh3_morph.py`) and own wheels/brakes (`_claude_stage\wheels\`, 16 files + DB fields). Then test: steering pivot, hands, needles, exhaust, mirror, wheel size, calipers.
- [ ] **Textures (TODO list, user 2026-10-12):** v11 installed 2026-10-12 and works (user) (the earlier "not lining up" report was v10 with W427 lamp textures). Check lamp placement in game; then mip chain, badges, scratches. Rollback: `roundtrip\backup_v11pre\`.
- [ ] Step 2, v11 (installed 2026-10-12): own nodamage/damage/lights/damagelights atlases from FH3 library textures and lens tints, textured lamp lenses (red tail segments are the brake material, amber inner segment, ridged headlight lens). Install after the v10 result, then check brake/head/side lights, lens look and whether the mip-less .xds files load.
- [ ] Step 2 next: badges/emblems from the FH3 symbols texture; scratch marks via a body UV unwrap into the damage atlas; real mip chain in `xds_write.py`.
- [ ] Torana damage feels light (user, v11). Knobs: K in `fh3_morph.py` (1.0 now), or blend in FH3 MBuf element 1 (larger offsets, unused). Part detachment (bumpers falling off) is a separate mechanism, not looked at yet.
- [ ] Give FM2 and FM3 ports damage: fill their empty Morph buffers (flag 1). FM2/FM3 have their own damage data to convert, or use the W427-style transfer as a fallback.
- [ ] Use the decrypted FH3 DB (`game-content\FH3-DB-STR\`), `port_fh3_db.py` and `fh3_colors.py` for every FH3 car; move them into `tools/dlc/`. The paint list currently comes from `torana_colors.json`; make the porter read each car's `manufacturercolors.bin` directly.
- [ ] Shadow lighting on some areas is slightly off (user, RT-B and Torana). Later: tune the bake (ray length, bounce/ambient floor for hidden parts, per-material calibration, more native cars in the calibration).
- [ ] Torana step 2: texture detail (atlas) for lights and lenses (brake lights show as dots), grille and badges.
- [ ] Torana standalone: own FM4 root/Model16/material blocks from FH3 locators, skeleton and lights data; own wheels and brakes (FH3 wheel is parametric; W427 models, DB now 14-inch); audio (Mustang-based now); thumbnail; moving gauge needles; steering pivot and driver hands.
- [ ] Writer leftovers: tangent from UV set 1, glass particles (dropped), header float 13 (written 0), morph buffers (see above).
- [ ] Move the FH3 builder (`_claude_stage\torana_clean\`), the baker and atlas decoder (`_claude_stage\roundtrip\bake\`) and `fh3_mesh.py` into `tools/dlc/`. The baker needs gcc + numpy + scipy (runs in the cloud container).
- [ ] Rebuild the Monaro and Mustang with the clean builder once the Torana is confirmed.
- [ ] FH3 roster comparison against FM4 (not done yet).

## Open: FM3 pipeline
- [ ] Lighting and material mismatch between preview and race on every FM3 car. New lead: check vertex words 12-15 and both UV sets in the FM3 converter output against a native FM4 car (same method as the FH3 writer test). Highest priority.
- [ ] Check FM3 converter output for empty Morph buffers (no damage), as found on the Torana.
- [ ] Brakes not visible in game (deferred).
- [ ] Audio: CMT/ET still from the Spark donor.
- [ ] Race-test the other 9 batch 1 cars.
- [ ] Remaining FM3 disc 1 cars (911 GT1, Audi A4 Touring needs a decision) and disc 2 cars (extract STFS first).

## Open: FM2 cars
- [ ] Elise springs variant if the go-kart feel persists.
- [ ] Install and test FM2 batch 1 (offers 87 to 89, staged).
- [ ] Batch the rest of the FM2 stock roster.
- [ ] Known gaps on every FM2 car: wheels, textures, emitters, damage morphs (empty Morph buffers, same cause as the Torana), vertex bytes 28 to 59 (check for baked ambient SH as in FM4 words 12-15).

## Open: FH and FH2 base cars (Tier 1, 37 cars)
- [ ] Port Tier 1 cars 2 to 11 as one pack, after one test car. Check every id that leaves the car's range.
- [ ] Port Tier 1 cars 12 to 37.
- [ ] Race-test the FH DLC cars already installed.
- [ ] LaFerrari leftovers: upgrade menus, transmission row, audio.

## Open: GT6 wheels
- [ ] GT6 wheels: collect test feedback on 57XV/TE37V (v4), then port the remaining 5 (he021 Triniti V, he075 57Xtreme, he078 57Gainer, he079 57Ultimate, he047 TE37SL); rebuild once to show full-brand names ("Volk Racing ..."). Display names must be full brand + model.

## Open: renderer and performance
- [ ] Native D3D12 executor (SDK patch 0002) is opt-in; decide on default. See `docs/NATIVE_RENDERER.md`.
- [ ] Reduce EDRAM ownership-transfer cost at 2x.
- [ ] Find which resolves FM4 reads on the CPU (readback cost).
- [ ] Car-thumbnail generation (blank thumbnail in a fresh profile).
- [ ] Interception renderer plan, steps 2 to 5.

## Open: DLC support
- [ ] Validate Launch Track Pack tracks, Porsche Expansion, later packs. See `claude/dlc-support.md`.

## Blocked: needs a new manufacturer or logo first
- [ ] Eagle, Bowler, Proton, Bus/Kenworth, PG make.

## Not started: tracks
- [ ] Track conversion, FM3 first, then FM2. See `claude/track-conversion-plan.md`.

## Housekeeping
- [ ] `_to_delete` folders: repo `_to_delete\` (old Aveo, Elise, Monaro, Commodore packs, stray `.bak`) and `.local\work\_to_delete_2026-10-11\` (RT test cars, Torana T1/T2, old offer-101 extracted folders v6 and v7). Delete once the user approves.
- [ ] Note: a recomp rebuild (user, 2026-10-12) cleared `fm4-recomp\.local\work\`; the `_to_delete_2026-10-11`, `holden_cleanup_2026-10-11`, `fh3_offers_removed_2026-10-11` and `project-docs` folders are gone. Torana zip backups remain in `roundtrip\backup_v*pre\`. Keep anything that must survive outside `.local\work` (docs now in `Forza-4-compiling-stuff\_claude_stage\project-docs\`, Torana backups in `fm4-recomp\.local\torana_backups\` or `roundtrip\`).
- [ ] Leftover strings from removed test packs in the string tables. Drop them on the next rebuild.
- [ ] Commit `tools/dlc/`, the launchers and the docs; most of this work is untracked in git.

## Tools
- FH/FH2: `port_dlc_car.py`, `carbin.py`, `make_fh_base_pack.py`, `stfs_extract.py`.
- FM3: `batch_fm3.py`, `make_fm3_base_pack.py`, `carbin_fm3.py`, `fm3vert.py`.
- FM2: `make_fm2_base_pack.py`; `carbin_fm2.py` in `fm2_converter\`.
- FH3: `fh3_grub.py` (repo); builder, `add_morph.py`, writer tests, baker and atlas decoder in `Forza-4-compiling-stuff\_claude_stage\`; DB porter and `fh3_colors.py` in `_claude_stage\torana_clean\db\`.
- Shared: `forza_zip.py` (set `FZLZX=~/tt/libfzlzx.so` in the device VM), `forza_str.py`, `forza_str_write.py`, `fzlzx.c` and `build_fzlzx.sh`, `dlc_inspect.py`, `xex_to_pe.py`.
- Trace: `fm4.log` in `.local\work\fm4\out\build\win-amd64-release\` (file trace launcher).
