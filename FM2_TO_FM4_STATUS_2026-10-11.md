# FM2 to FM4 conversion: current state and what is left (2026-10-11)

Written for anyone picking this up cold. Figures come from the 2026-10-11 device scan, the FM2 handover (2026-10-10 14:54 UTC), `claude/fm2-carbin-format.md`, and `tools/dlc/fm2_port/README.md`. Where a number is an estimate, it says so.

## What "full" means here

A full FM2 conversion means a car that: appears in the FM4 dealership and garage, shows its own body, wheels, textures and livery, has its own engine sound and model audio, has its own damage and close-up models, drives with FM2-derived physics, and races without crashing. Anything the converter does not yet carry over from FM2 is taken from a donor FM4 car, so that part is not converted.

Against that definition, no FM2 car is fully converted yet. The Elise 135R is the first FM2 car that loads and races in FM4, and it still uses the FM4 Elise 111S's wheels, textures, audio and emitters.

## Where things stand

| Item | State |
|---|---|
| Lotus Elise 135R (FM2 car 332) | Installed as offer 86, car 1770, engine 1184, drivetrain 1284, mapping 6423, wheel 11813, string priority 25310. Confirmed working in game: showcase, preview, race, and the name shows correctly. Drives "like a gokart". Springs variant (`--springs fm2`) staged, not tested. |
| Lotus Exige 2005 (`LOT_Exige_05`) | Converted offline and staged as offer 87 (car 1771, engine 1185, drivetrain 1285, wheel 11814, mapping 6424, string priority 25610). Donor Exige 240. Supercharger stripped (FM2 car is naturally aspirated). Not installed. Not raced. |
| TVR Tuscan R (`TVR_TuscanR_01`) | Converted offline and staged as offer 88 (car 1772, engine 1186, drivetrain 1286, wheel 11815, mapping 6425, string priority 25620). Donor Tuscan S. Rear taillights may not light, since the FM2 model has no taillight parts. Not installed. Not raced. |
| Mitsubishi Lancer Evo VIII GSR (`MIT_LancerEvoVIIIGSR_04`) | Converted offline and staged as offer 89 (car 1773, engine 1187, drivetrain 1287, wheel 11816, mapping 6426, string priority 25630). Donor Evo VIII MR. One extra part (`bumperFrameF`). Not installed. Not raced. |
| Staged batch folder | `game-content\DLC-FH\_FM4-ports\fm2_batch1\` (Exige, Tuscan, Evo). Scratch build logs in `$HOME/fm2work/pack_batch/` (not persistent). |
| Converter `carbin_fm2.py` | Works offline for the Elise, Exige, Tuscan and Evo: parser, index bounds, bounding boxes within 8.2e-4 m, positions, normals and UVs all checked. Lives in the game-data folder (`fm2_converter\`), not yet copied into `tools/dlc/`. |
| Pack builder `make_fm2_base_pack.py` | In the repo (untracked by git). Flags: `--car`, `--donor`, `--donor-pack`, `--real-model`, `--physics fm2|donor`, `--springs fm2|donor`, `--lod0-parts donor|all`. Includes a check for aspiration and supercharger mismatches, and the name-namespace fix. |
| Port framework `tools/dlc/fm2_port/` | Step 1 only (read-only planning): `inventory.py`, `donor.py`, `port_fm2_car.py` (`--car`, `--donor`, `--report`, `--roster`). Nothing in it writes to the game. |
| Roster scan (2026-10-11) | 310 FM2 media names checked: 188 already in FM4 base, 19 in an installed or staged pack, 13 with no FM4 make, 89 ready, 1 ready with a weak donor. "Ready" means the donor and ids check out, not that the car has been converted. |
| Tier 3 stock cars (`claude/port-priority-list.md`) | 13 left after removing the GT-500KR and 2007 GT500, which FM4 already has under other names. Plus about 29 tuner and variant builds and 11 race cars. |

## Installed packs to know about

- Offers 1 to 4, 21 to 39, 70 to 86, 99 and 100 are installed, per the 2026-10-11 scan.
- An unidentified pack at string priorities **25400** and **25500** (folder `CCB0FD3D...`, modified 10-10 09:05, after our Elise install). It has Elise name strings too. Its origin is unknown and it was not found under the scanned roots. Ask the user before touching it.
- Offer 99 (Monaro variants) and offer 100 (FH3 Mustang) belong to another thread. Do not change them while working on FM2.

## Known gaps, per car

1. **Wheels.** The FM2 wheel carbin is version 1 and is not converted. Every FM2 car uses the donor's wheels.
2. **Textures.** FM2 `.xds` files are not converted. Textures come from the donor.
3. **Audio.** FM2 audio is not mapped to FM4's format. Engine and model audio come from the donor.
4. **Emitters.** Copied from the donor. The FM2 emitter frame is not decoded.
5. **Damage.** No damage morphs. The FM2 morph data is empty.
6. **Close-up and LOD0.** The Elise is built with the close-up layout, but it is not clear the other cars match it.
7. **Vertex data.** Bytes 0 to 27 and 36 to 59 are decoded only in part. Bytes 28 to 35 use a fitted scale of 1.5 that is not confirmed.
8. **Part names.** FM2 names differ from FM4's (`taillightL/R` vs `glassLTL/RTL`, `exhaustL/R` vs `exhausta`, no `wheel` or `undercarriagea`, `EXCLUDE_bumperRrace` present). No alias table exists yet.
9. **Carbon fibre.** FM4's carbon_fiber material UVs are about 240 times the FM2 values, so wing weave can look magnified.
10. **Cockpit, brakes and gauges.** Taken from the donor.
11. **Physics.** The donor's engine mass, inertia, tyre compounds, friction curves and aero are kept. Springs are on FM4's scale but the dampers are not (bump 1.77 vs 2.70, clamp 30 vs 10, bumpstop 120 vs 200). The default is `--springs donor`. The donor for each car is chosen by model, then brand and class, then year. No donor exists yet for the SEAT Cupra GT Prototype or the Proton Spirra (Proton is not in FM4).

## Key technical facts

- **Car-name namespace.** Car `DisplayName` and `ModelShort` must have a high 16-bit word of **0xCD49**. FM2 keys use 0xA949, which the game does not resolve, so names were blank. `make_fm2_base_pack.py` rewrites them. Offer names use 0xB8D0 and are fine as they are.
- **String tables are cumulative per pack.** `port_dlc_car.py` only sees installed packs, so staged batches can overwrite each other's name keys unless built against a view that includes the staged packs.
- **Zip member methods.** Our staged packs use method 0 (stored). FM4 packs use method 21 (LZX). Both load in game so far.
- **Id rules.** Engine E and drivetrain E+100 for the same car. Torque curves are keyed engine×1000, so the engine owner id must also be free in the torque keys. String priorities are in the 25xxx range. Full table: `claude/car-port-handoff.md` section 5.
- **Offers.** Two-digit decimal, ContentOffers text id `4D530910F4FF%02d00`. Offer 98 is the next free number under the two-digit scheme, per the 2026-10-11 scan.

## Known bugs

- `tools/dlc/batch_fm3.py --plan` proposes an invalid offer. It picked 101 (a 17-character id) in one run and 100 in another. Replace its offer choice with `inventory.pick_offer` from `fm2_port/`. This is a two-line fix.

## Remaining work, in order

1. **Race-test the staged batch.** Confirm the game is closed, back up the live folders, install offers 87 to 89 one at a time, and race each one. Start with the Exige, since it checks the supercharger fix, then the Tuscan (taillights), then the Evo.
2. **Fix the batch_fm3 offer bug** (above). This is a prerequisite for batch work.
3. **Finish the port framework** (`tools/dlc/fm2_port/`, step 2 onwards):
   a. Id injection in `make_fm2_base_pack.py`. The allocator output must be applied to the mini database (Data_Car, Data_Engine, Data_Drivetrain, List_Wheels, ContentOffers, ContentOffersMapping, torque keys) and to the pack names. The Elise output must stay byte-identical. The FH path in `port_dlc_car.py` already remaps ids, so reuse it.
   b. The part alias table `fm2_part_alias.json`, agreed with the user (keep Elise byte-identical after any change).
   c. Verification of each built pack: zip member check, merge row check, and a check of ids that leave the car's range (these hang the race load silently).
   d. An explicit `--install` with a game-closed confirmation and a backup into `_to_delete\`.
4. **Copy `carbin_fm2.py` into `tools/dlc/`**, with its tests (`test_carbin_fm2.py`), so the pipeline lives in the repo.
5. **Decode the FM2 wheel carbin (version 1).** This is the largest visible gap after the body, and it must come before textures.
6. **Textures.** Convert FM2 `.xds` files, using `claude/fh3-swatchbin-xds-format.md` as a starting point if the format is related.
7. **Audio.** Map FM2 engine and model audio to FM4's format.
8. **Batch the rest of Tier 3.** Thirteen stock cars, one donor each, ids checked against the table. Then the tuners and race cars, if the user wants them.
9. **Identify the unknown pack** at string priorities 25400 and 25500 before removing anything.
10. **Decide the physics policy with the user**: donor springs or FM2 springs as the default, and whether the donor should be the FM4 sibling or a class-based pick.

## Estimates

Rough and unverified. Converting the Exige, Tuscan and Evo to the current state is done. The Elise shows that the body, name and physics path works. The time-consuming parts are the wheel and texture converters, which are new formats, and the audio mapping. Each of these has to be validated on at least two cars before it can be trusted for the batch.

## Files

- Project: `claude/fm2-to-fm4-status-2026-10-11.md` (this file), `claude/fm2-carbin-format.md`, `claude/fm2-port-handover-2026-10-10-1454utc.md`, `claude/fm2-format-web-research.md`, `claude/fm2-base-roster-gap.md`, `claude/port-priority-list.md`, `claude/global-todo.md`.
- Repo: `tools/dlc/make_fm2_base_pack.py`, `tools/dlc/fm2_port/`, `tools/dlc/batch_fm3.py`, `docs/CAR-PORT-HANDOFF.md`, `GLOBAL_TODO.md`.
- Game-data folder: `fm2_converter\` (`carbin_fm2.py`, `test_carbin_fm2.py`, `CONVERTER_NOTES.md`, `fm2_structure.md`, `fm2_parts.json`, `fm2_parts.md`, and `lot_elise135r_03_fm4.carbin`), and `game-content\DLC-FH\_FM4-ports\fm2_batch1\`.
- Moved aside (in the repo's `_to_delete\`): `fm2_elise1770_donorphys_2026-10-10`, `fm2_elise1770_lod0v1_2026-10-10`, `fm2_elise1770_nsname_bad_2026-10-10`.
