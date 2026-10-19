# Ongoing agent handover: Forza car ports into FM4 (2026-10-11 05:35 UTC, 16:35 AEDT)

This is the master handover. Another AI agent should read this file first, then the linked docs for any thread it touches. It replaces nothing: the thread-specific handovers and status docs still hold the detail. It was written from the project docs and a device scan at the time above. Trust the device state over this file if they disagree, and say so when you update it.

Clocks: the cloud shell and the device report UTC. The user is in Sydney (AEDT, UTC+11). Log file names use device time. Check file timestamps before trusting any "latest" claim.

## 1. Mission and definition of done

Port cars from earlier Forza games (FM2, FM3, FH, FH2, FH3) into Forza Motorsport 4 (FM4) on the user's Windows PC, as DLC packs. A car counts as done when it:
- appears in the dealership and garage under the right make,
- shows its own body, wheels, textures and livery,
- has its own engine sound and model audio,
- drives with sensible physics,
- races without crashing.

Anything still borrowed from a donor FM4 car does not count as converted, and the status docs say which parts are borrowed. Current state: several FM3 cars run and drive, but none is fully converted. The FH and FH2 DLC cars are installed and only partly tested.

## 2. Standing constraints (from the user, keep these)

- Never delete user files. Move them into `_to_delete\` in the repo. Deleting needs the user's approval, which is requested through the device permission tool.
- Close FM4 before moving or installing packs. A move while the game runs fails with "Permission denied".
- Back up before changing any live pack.
- Do not touch offers owned by another thread (see section 5). Check with the user before changing one.
- Do not send the user's email to any unrelated service.
- Analysis can proceed without asking. Pick the most sensible option, say what you chose and why, and report it. Stop and ask before installs that replace a working pack, before any deletion, and before anything that changes the user's game settings.
- The user prefers plain prose, short status updates, and no formatting clutter.

## 3. Environment and access

- Device: `sancho` (Windows PC). It is reached through the remote-devices tools (`mcp__remote-devices__device_bash`, `device_list_dir`, `device_stage_files`, `device_commit_files`, `device_request_folder_access`, `device_request_delete_permission`). These sometimes disconnect. Check with the device info tool before a long job. If the device is offline, say so and do what the cloud can do.
- Connected folders (mount points inside the device VM):
  - Repo: `C:\Users\Me\Documents\GitHub\fm4-recomp` -> `$HOME/mnt/fm4-recomp`
  - Game data: `C:\Z-Temp\Forza-4-compiling-stuff` -> `$HOME/mnt/Forza-4-compiling-stuff` (access was granted; may need re-requesting in a new session)
- Installed DLC lives at `fm4-recomp\.local\user\0000000000000000\4D530910\00000002\<package>` with `Headers\00000002\<package>.header` beside it. Packages are extracted folders with zips inside `Media\DLCZips\`.
- Each `device_bash` call has a limit of about 180 s. Background jobs started from the device shell (nohup, setsid) die when the call ends. Run jobs in the foreground and keep them under 170 s. Scratch files go under `$HOME/<name>` in the VM, which is not visible to the user and is lost when the session ends. Keep anything the user needs inside the mounted folders.
- The LZX helper library must be built once per session: `mkdir -p $HOME/fzbuild && sh tools/dlc/build_fzlzx.sh $HOME/fzbuild/libfzlzx.so`, then `export FZLZX=$HOME/fzbuild/libfzlzx.so`. Needed for method-21 zips.
- Do not create stray backup files inside `tools/dlc` (a `cp` to `.bak` there caused a clean-up job). Use `$HOME/<scratch>` for backups.
- The project memory tools are restricted to the project's own subtree. The Claude_Docs and memory servers were disconnected at the end of the session; do not depend on them.
- Google Drive is not connected in this session. "On my drive" has meant the connected PC folder.

## 4. Document index

Project docs (`claude/`):
- Master and handovers: this file; `handover-2026-10-10-1457utc-fm3-car-port.md` (FM3 thread); `handover-2026-10-10-1457utc-fh3-mustang-monaro.md` (FH3 Mustang and Monaro); `fm2-port-handover-2026-10-10-1454utc.md` (FM2 thread).
- Status: `fm3-port-status.md` (FM3, most current), `fm3-db-port-findings.md`, `fh3-port-status.md` (FH3), `fh3-mustang-gt500-port.md`, `fh3-torana-test-clones-2026-10-11.md`, `fh3-name-cipher-and-torana-2026-10-11.md`, `fm2-to-fm4-status-2026-10-11.md` (FM2 full status), `fm2-carbin-format.md`, `global-todo.md`.
- Formats: `fh3-modelbin-format.md`, `fh3-swatchbin-xds-format.md`, `fh-car-port.md`, `fh-vs-fm4-dlc-layout.md`, `fm3-vs-fm4-dlc-layout.md`, `car-asset-layout.md`, `fm2-format-web-research.md`, `forza-studio-source-notes.md`, `fh2-laferrari-race-hang.md`, `dlc-support.md`, `car-import-findings.md`.
- Planning: `car-port-handoff.md` (general handoff, id table in section 5), `port-priority-list.md` (ranked car lists), `track-conversion-plan.md`, roster gap docs (`fh-`, `fh2-`, `fm2-`, `fm3-base-roster-gap.md`).

Repo files:
- `GLOBAL_TODO.md`, `docs/CAR-PORT-HANDOFF.md` (repo copies of the todo and handoff), `FM2_TO_FM4_STATUS_2026-10-11.md`, and this handover as `FM3-to-FM4-AGENT-HANDOVER.md`.
- Tools in `tools/dlc/`: `batch_fm3.py` (FM3 batch driver), `make_fm3_base_pack.py`, `carbin_fm3.py`, `fm3vert.py`, `make_fh_base_pack.py`, `make_fm2_base_pack.py`, `port_dlc_car.py`, `carbin.py` (FH transcoder), `forza_zip.py`, `forza_str.py`, `forza_str_write.py`, `stfs_extract.py`, `xex_to_pe.py`, `fzlzx.c`, `build_fzlzx.sh`, `dlc_inspect.py`, `fh3_grub.py` (FH3 bundle reader), `fm2_port/` (FM2 framework, step 1 only).

Game-data folder (`Forza-4-compiling-stuff`): `extracted\Media` (FM4 base), `game-content\FM3U1` (FM3 disc 1), `FM3U2` (FM3 disc 2 STFS packages), `FM2`, `FH`, `FH2`, `DLC-*` (raw packages), `DLC-extracted\fm4-dlc-merges\`, `DLC-FH\_FM4-ports\` (staged packs, including `fm2_batch1\`), `fm2_converter\` (FM2 converter and notes), `_claude_stage\mustang\` (FH3 builds, backups, variants, notes).

## 5. Active threads

### A. FM3 cars (highest activity)
Working: the Aveo5 LT (offer 85, car 1769) with the real FM3 body and real brakes; loads, drives and sits correctly in a race (user test). Batch 1 (offer 84, 11 cars, car ids 1758 to 1768, donor brakes): Boxster S and Monte Carlo SS Stock Car confirmed running and driving; the other nine are menu-checked only. Skipped: Audi A4 Touring (no `carattribs.xml`).

Open, in priority order:
1. **"VW Jetta type body"** (reported by the user, not yet explained). Car and offer are not identified. Ask the user which car shows it. Planned sanity check: rebuild the car with `make_fm3_base_pack.py --donor <FM4 car>` and WITHOUT `--real-model`, so an FM4 body shows. If the donor body is right and the FM3 body is wrong, the fault is in the body conversion. If both look the same, look at the database or model reference. This check has not been run.
2. **Lighting and material mismatch in race** on all tested FM3 cars. Colours and materials differ from the preview cameras. Cause not identified. Compare FM3 and FM4 material and shader data (livery masks, material parameters, shader technique names) for one car, using the Aveo as the test. This affects every FM3 car, so it is the top priority after item 1.
3. **Brakes not visible** in game, even though the converted brakes are in the offer 85 pack. Candidates: geometry placement, the `_lod0` naming, or materials. Deferred by the user.
4. **FM3 audio** (CMT and ET) still comes from the Spark donor for all FM3 cars.
5. **Race-test the nine untested batch 1 cars**, at least one road car and one more stock car.
6. **FM3 disc 2 cars** (Pontiac GTO, Dodge SRT4, HKS Genesis, Toyota Tom's, Mini Coupé Concept and others): extract the STFS packages with `stfs_extract.py`, then run the batch driver.
7. The Aveo can be rebuilt as a batch car with real brakes later. Do this only after item 3, and move the old pack out first.

Tools: `batch_fm3.py --plan --cars <list>` allocates ids. Then `--install` runs the pack build and port (see section 7). The pipeline is proven end to end for FM3 disc 1 cars. The converter converts body, lod0, cockpit, wheels and brakes structurally for all 311 disc 1 car zips, but geometry is confirmed in game only for the Aveo, Boxster S and Monte Carlo SS.

### B. FM2 cars
Working: the Lotus Elise 135R (offer 86, car 1770) is installed and was confirmed in game. Showcase, preview, race and name all work. Drives "like a gokart". The springs variant (`--springs fm2`) is staged and untested.

Staged, not installed, not raced: Lotus Exige 2005 (offer 87, car 1771), TVR Tuscan R (offer 88, car 1772), Mitsubishi Lancer Evo VIII GSR (offer 89, car 1773). They are in `DLC-FH\_FM4-ports\fm2_batch1\`. Install only after the game is closed and the user confirms. Test the Exige first (supercharger fix), then the Tuscan (taillights may not light), then the Evo.

Gaps for every FM2 car: wheels (FM2 wheel carbin is version 1, not converted), textures (`.xds` not converted), audio (not mapped), emitters (donor copy), damage morphs (none), vertex bytes 28 to 35 (fitted scale, not confirmed) and 36 to 59 (not decoded), part-name aliases (no table yet), and carbon-fibre UVs (too magnified on some wings).

Framework: `tools/dlc/fm2_port/` has step 1 only (read-only planning: `inventory.py`, `donor.py`, `port_fm2_car.py --roster` and `--car`). Next steps are in `fm2-to-fm4-status-2026-10-11.md`: id injection into `make_fm2_base_pack.py`, the alias table, pack verification, and an explicit install step.

Roster scan (2026-10-11): 310 FM2 media names checked. 188 are already in FM4 base, 19 are in an installed or staged pack, 13 have no FM4 make, 89 are ready, 1 is ready with a weak donor. Tier 3 stock cars left: 13.

Unknown: an installed pack at string priorities 25400 and 25500 (folder `CCB0FD3D...`) with Elise name strings. Its origin is not recorded. Ask the user before removing anything.

Known bug: `batch_fm3.py --plan` proposes invalid offers (101, a 17-character id, in one run). Replace its offer choice with `inventory.pick_offer` from `fm2_port/`. Two-line fix; it is a prerequisite for batch work.

### C. FH3 cars under the Holden make (another thread; read its handovers first)
- Offer 99: Monaro variants (`hol_monarogts_73v1` as car 9110, `v2` 9111 and `v3` 9112). v2 and v3 use the Shelby body and race cleanly. v1 has never been seen in game, and its body is unexplained. Its UV transforms are out of range (450 of 660 meshes), which may be related to the textureless Mustang body but is not confirmed.
- Offer 100: FH3 Mustang GT500 test (car 9210, `HOL_FH3GT500_13`). Live v3 loads and races, but the body is black (textureless) because UV transforms fall outside 0..1 and FM4 clamps them. Only the main body carbin is FH3; the rest is the Shelby's. v4 (UV fold) crashes the race load (null list entry in `sub_82830320`). Handover: `handover-2026-10-10-1457utc-fh3-mustang-monaro.md`.
- Offer 101: four Torana test clones (cars 9310 to 9313, `HOL_TORANA_77`). A (baseline) loads and races, paint works in preview only, other colours go black in race. B, C and D show a grey car-select window; the trace shows the clone stalls on a missing `colorshadersettings1.xml` request. Next step: compare the baseline and clone request sequences from the same run. Docs: `fh3-torana-test-clones-2026-10-11.md`, `fh3-name-cipher-and-torana-2026-10-11.md`.
- The FH3 zip name cipher (scrambled entry names) is decoded; the table is in `fh3-name-cipher-and-torana-2026-10-11.md`.
- Textures convert (`.swatchbin` to Xbox 360 `.xds`, round-trip verified), but FH3 UVs tile and need a material mapping.

Do not change offers 99 or 100 without checking with the user and the FH3 handover. Offer 101 is also in this thread.

### D. FH and FH2 cars (earlier packs)
- 40 FH DLC cars are installed (offers 70 to 81). The GT500 and Honda pack run in game. The other 36 have not been raced.
- LaFerrari (FH2 base car, offer 82, car 1756) is installed and loads in menus. Race status not recorded.
- Tier 1 FH and FH2 base cars (cars 2 to 37 in `port-priority-list.md`): not yet ported. Use `make_fh_base_pack.py`, then `port_dlc_car.py`.
- The FH2 race hang and its cause are in `fh2-laferrari-race-hang.md`.

### E. Tracks
Not started. The plan is in `track-conversion-plan.md`. Do this only after the car work is settled.

## 6. Ids and offers

Full id table: `car-port-handoff.md` section 5. Summary of the highest ids in use as of the 2026-10-11 scan:
- Offers in use: 1 to 4, 21 to 39, 70 to 86, 99, 100, 101 (the scan missed 101, which exists as the Torana pack). Staged-only: 87 to 89. Next free two-digit offer: 98 is free, but offers 97 and 98 were the removed Monaro tests, so check on disk before using it. Prefer 102 or above after confirming it is free.
- Largest ids in use: car 9310 (Torana clones), engine 9260, drivetrain 9230, mapping 10010, wheel 90400, string priority 25700 (Mustang) and higher for Torana (check).
- ContentOffers ID: `4D530910F4FF` + two-digit decimal offer + `00` for offers 70 and up (for example 85 = `4D530910F4FF8500`). Other schemes exist for older packs; check any new id against every installed merge.
- Engine and drivetrain bases must be at least 100 apart. Torque curves are keyed engine × 1000, so an engine owner id must also be free in the torque keys. Overlapping ids break race loads silently.
- Always check ids against the installed merges and the staged packs (`DLC-FH\_FM4-ports\`) before use. The batch driver does this for FM3 but has the offer bug above.

## 7. Safe install procedure

1. Confirm with the user that FM4 is closed.
2. Build the pack into a scratch folder first. Check the zip members, the merge rows and any id that leaves its car range.
3. Move the live package folder and its header out of the live folders into `_to_delete\<name>_<date>\`. Use `mv -n`. If the move is refused, stop: the game is probably running.
4. Run the port tool with `--install` (or copy the staged package into the live folders), then confirm the `<offer>.puboffer` file is present in the new package folder.
5. Ask the user to test. Record the result in the thread's status doc with the offer number and date.
6. Remove nothing. Old packs stay in `_to_delete\` until the user approves deletion.

## 8. Known pitfalls

- A sqlite id clash breaks races silently. Engine and drivetrain bases must stay 100 apart.
- Empty triangle arrays need shape (0, 3); a 1-D empty array broke the wheel and brake path.
- Car names must use the 0xCD49 namespace in the car name keys (FM2 keys use 0xA949, which the game ignores).
- String tables are cumulative per pack. Staged batches can overwrite each other's keys unless built against a view that includes the staged packs.
- Method-0 (stored) zips load in game, but FM4 packs use method 21 (LZX). Both have worked so far.
- A `cp` into `tools\dlc` left a `.bak` file that had to be moved out. Keep backups in scratch.
- The batch driver skips cars already installed. To rebuild a car, move the old pack out first.
- Several `_to_delete\` folders hold old packs; the stray backup from this session is in `_to_delete\stray_backups_2026-10-10\`.
- Background jobs die when a device call ends. Run long steps in the foreground.

## 9. Open questions for the user

1. Which car shows the "VW Jetta type body", and in which offer?
2. Did the user test the nine untested batch 1 cars? Results per car.
3. Results of the FH3 offers 99 and 100 tests (the user was asked to test them).
4. What the user saw for the Monaro v1 (no body, a crash, or a wrong model).
5. Can the unknown pack at string priorities 25400 and 25500 be removed?
6. Should the Torana clones (offer 101) stay installed?
7. Which FM2 and FM3 cars the user wants next after the current batches.

## 10. Suggested next steps for the next agent

1. Ask the user the open questions (section 9), in one message.
2. Run the Jetta sanity check (donor body, no real body) on the Aveo or the batch car the user names. Report which body shows.
3. Investigate the lighting and material mismatch on the Aveo. Start by comparing the FM3 and FM4 livery masks and the shader technique names for the body.
4. Fix the `batch_fm3.py --plan` offer bug (switch to `pick_offer`).
5. Install and race-test FM2 batch 1 (offers 87 to 89), one car at a time, with the user's confirmation that FM4 is closed.
6. Continue the FM2 framework (id injection first). Keep the Elise output byte-identical.
7. For the Torana clones, compare the baseline and clone request sequences from the same trace.
8. Keep `global-todo.md` and this handover current. Update the status doc for any thread you touch.

## 11. Updating this handover

When you finish a session's work, update: the thread status in section 5, any new offers or ids in section 6, new open questions in section 9, and the date and time in the title. Write the full file back to the project with `project_write`, and update the repo copy (`FM3-to-FM4-AGENT-HANDOVER.md`) the same way.
