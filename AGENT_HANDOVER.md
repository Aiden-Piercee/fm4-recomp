# Ongoing agent handover: Forza car ports into FM4 (2026-10-11 late AEDT)

## Project rules (read first; full list in the claude.ai project doc `claude/00-project-rules-read-first.md`)
- The base `gamedb.slt` is overridden by the Damage Plus pack (offer 170, `DmgDB_pri_27100.zip` at `game:\Media\db\`). DLC merges are `INSERT OR IGNORE` (add rows only). Change existing base rows only through `tools/dlc/make_damage_pack.py --db-sql` and rebuild; never edit the loose `gamedb.slt`, never add a second `game:\Media\db\` override.
- Every car must have damage: after installing or rebuilding any car pack, rebuild and reinstall Damage Plus (it overrides every car's carattribs.xml and fills missing morph data with `tools/dlc/fill_morph.py`).


This is the master handover. Another AI agent should read this file first, then the linked docs for any thread it touches. The thread-specific handovers and status docs still hold the detail. Trust the device state over this file if they disagree, and say so when you update it.

Clocks: the cloud shell and the device report UTC. The user is in Sydney (AEDT, UTC+11). Log file names use device time. Check file timestamps before trusting any "latest" claim.

## 1. Mission and definition of done

Port cars from earlier Forza games (FM2, FM3, FH, FH2, FH3) into Forza Motorsport 4 (FM4) on the user's Windows PC, as DLC packs. A car counts as done when it:
- appears in the dealership and garage under the right make,
- shows its own body, wheels, textures and livery,
- has its own engine sound and model audio,
- drives with sensible physics,
- races without crashing.

Anything still borrowed from a donor FM4 car does not count as converted, and the status docs say which parts are borrowed. Current state: several FM3 cars run and drive, but none is fully converted. The FH and FH2 DLC cars are installed and only partly tested. FH3: the converter writer is now fixed and verified; a clean Torana A9X port is installed and awaiting its first good in-game test.

## 2. Standing constraints (from the user, keep these)

- Never delete user files. Move them into a `_to_delete` folder. Deleting needs the user's approval, which is requested through the device permission tool.
- Close FM4 before moving or installing packs. A move while the game runs fails with "Permission denied".
- Back up before changing any live pack.
- Do not touch offers owned by another thread (see section 5). Check with the user before changing one.
- Do not send the user's email to any unrelated service.
- Analysis can proceed without asking. Pick the most sensible option, say what you chose and why, and report it. Stop and ask before installs that replace a working pack, before any deletion, and before anything that changes the user's game settings.
- The user prefers plain prose, short status updates, and no formatting clutter.
- Change one thing per test. Prove pipeline steps on a native FM4 car before trusting them on a ported car ("inside-out" testing worked: see section 5C).

## 3. Environment and access

- Device: `sancho` (Windows PC). Reached through the remote-devices tools (`device_bash`, `device_list_dir`, `device_stage_files`, `device_commit_files`, `device_request_folder_access`, `device_request_delete_permission`). These sometimes disconnect. If the device is offline, say so and do what the cloud can do.
- Connected folders (mount points inside the device VM):
  - Repo: `C:\Users\Me\Documents\GitHub\fm4-recomp` -> `$HOME/mnt/fm4-recomp`
  - Game data: `C:\Z-Temp\Forza-4-compiling-stuff` -> `$HOME/mnt/Forza-4-compiling-stuff` (may need re-requesting in a new session)
- Installed DLC lives at `fm4-recomp\.local\user\0000000000000000\4D530910\00000002\<package>` with `Headers\00000002\<package>.header` beside it. Packages are extracted folders with zips inside `Media\DLCZips\`. The game reads each offer's DB from the extracted `<offer>_pri_65\media\db\patch\` folder.
- Each `device_bash` call is limited to about 180 s, and background jobs die when the call ends (the sandbox kills children). Keep each step under 170 s; split long copies (big zips take 60-120 s).
- `device_stage_files` refuses files nested more than 7 folders deep; copy them higher up first.
- The device VM has no package network (no pip). Work that needs extra Python packages or compiled code (the ambient-light baker) runs in the cloud container; stage inputs up, commit outputs back.
- The LZX helper library must be built once per session: `sh tools/dlc/build_fzlzx.sh $HOME/tt/libfzlzx.so`, then `export FZLZX=$HOME/tt/libfzlzx.so`.
- Do not create stray backup files inside `tools/dlc`.
- Trace: `fm4-recomp\.local\work\fm4\out\build\win-amd64-release\fm4.log` (file trace launcher). A car's body load shows as a request for `cars\<NAME>\<NAME>.carbin` followed by `CarAttribs.xml`, `_lod0`, `_cockpit`.

## 4. Document index

Project docs (`claude/`):
- Master and handovers: this file; `handover-2026-10-10-1457utc-fm3-car-port.md` (FM3); `handover-2026-10-10-1457utc-fh3-mustang-monaro.md` (FH3 Mustang/Monaro, partly superseded); `fm2-port-handover-2026-10-10-1454utc.md` (FM2).
- FH3 (newest first): `fh3-torana-clean-port-2026-10-11.md`, `fh3-torana-fix-t1-t2-2026-10-11.md`, `converter-writer-test-step1-2026-10-11.md` (the key findings), `fh3-torana-review-2026-10-11.md`, `fh3-port-status.md`, `fh3-name-cipher-and-torana-2026-10-11.md`, `fh3-torana-test-clones-2026-10-11.md` and `fh3-torana-race-trace-2026-10-11.md` (superseded), `fh3-mustang-gt500-port.md`, `fh3-modelbin-format.md`, `fh3-swatchbin-xds-format.md`.
- Other status: `fm3-port-status.md`, `fm3-db-port-findings.md`, `fm2-to-fm4-status-2026-10-11.md`, `fm2-full-conversion-status-2026-10-11.md`, `fm2-carbin-format.md`, `global-todo.md`.
- Formats and layout: `fh-car-port.md`, `fh-vs-fm4-dlc-layout.md`, `fm3-vs-fm4-dlc-layout.md`, `car-asset-layout.md`, `fm2-format-web-research.md`, `forza-studio-source-notes.md`, `fh2-laferrari-race-hang.md`, `dlc-support.md`, `car-import-findings.md`.
- Planning: `car-port-handoff.md` (id table in section 5), `port-priority-list.md`, `track-conversion-plan.md`, roster gap docs.

Local copies on the PC: `C:\Z-Temp\Forza-4-compiling-stuff\_claude_stage\project-docs\` (outside the repo; a recomp rebuild clears `fm4-recomp\.local\work`) holds the main docs and the 2026-10-11 FH3 docs (snapshot; the project is the master copy). Repo root copies: `AGENT_HANDOVER.md` (this file), `GLOBAL_TODO.md`.

Repo tools in `tools/dlc/`: `batch_fm3.py`, `make_fm3_base_pack.py`, `carbin_fm3.py`, `fm3vert.py`, `make_fh_base_pack.py`, `make_fm2_base_pack.py`, `port_dlc_car.py`, `carbin.py`, `forza_zip.py`, `forza_str.py`, `forza_str_write.py`, `stfs_extract.py`, `xex_to_pe.py`, `fzlzx.c`, `build_fzlzx.sh`, `dlc_inspect.py`, `fh3_grub.py`, `fm2_port/`.

FH3 tools (not yet in the repo), in `Forza-4-compiling-stuff\_claude_stage\`: `mustang\tools\` (FH3 readers and the original writer), `roundtrip\` (writer tests, offer 101 pack builder, `bake\` ambient-light baker and atlas decoder), `torana_clean\` (clean FH3 builder).

## 5. Active threads

### A. FM3 cars
Working: the Aveo5 LT (offer 85, car 1769) with the real FM3 body and real brakes; loads, drives and sits correctly in a race. Batch 1 (offer 84, 11 cars, car ids 1758 to 1768, donor brakes): Boxster S and Monte Carlo SS confirmed; the other nine are menu-checked only. Skipped: Audi A4 Touring (no `carattribs.xml`).

Open, in priority order:
1. "VW Jetta type body" (reported by the user, not explained). Ask which car. Planned check: rebuild with `make_fm3_base_pack.py --donor <FM4 car>` without `--real-model`.
2. Lighting and material mismatch in race on all FM3 cars. **New lead:** check the FM3 converter's vertex words 12-15 (baked ambient SH) and UV sets the same way the FH3 writer was checked (`converter-writer-test-step1-2026-10-11.md`). Zero or wrong values there give exactly a preview/race lighting difference.
3. Brakes not visible (deferred).
4. FM3 audio still from the Spark donor.
5. Race-test the nine untested batch 1 cars.
6. FM3 disc 2 cars (extract STFS first).

### B. FM2 cars
Working: Lotus Elise 135R (offer 86, car 1770). Staged, not installed: Exige (87), TVR Tuscan R (88), Evo VIII GSR (89) in `DLC-FH\_FM4-ports\fm2_batch1\`. Gaps for every FM2 car: wheels, textures, audio, emitters, damage morphs, vertex bytes 28 to 59 (**check whether they also carry baked ambient SH**), part-name aliases, carbon-fibre UVs. Framework `tools/dlc/fm2_port/` is at step 1. Known bug: `batch_fm3.py --plan` offer choice (switch to `inventory.pick_offer`).

### C. FH3 cars (Holden make)
Key result (2026-10-11): FM4 car vertices carry baked per-vertex ambient lighting in words 12-15 (order-1 SH, s16/32767, order L0, y, z, x). The old FH3 writer zeroed it (matte black body) and put FH3 tiling UVs in UV set 1 (the atlas slot). Proven with W427 clones run through the writer: native = fine, current writer = broken, writer keeping words 12-15 = fine, our own bake = fine with small shadow differences. Details: `converter-writer-test-step1-2026-10-11.md`.

Installed: offer 101 = only car 9330 `HOL_TORANA_77` "Holden Torana A9X (FH3)", a clean port from the FH3 source (`fh3-torana-clean-port-2026-10-11.md`): own geometry, real wheelbase 2.586 m and tracks, W427 file layout and materials, baked lighting, atlas UVs per material. v5 gave a grey preview (DB lookup failed after the main carbin loaded); v6 set Data_CarBody/CarBodyID to 9330000 (= car*1000). v7 adds the FH3 model origin as-is and the car's own physics/upgrade rows from the user's decrypted FH3 DB (`game-content\FH3-DB-STR\gamedbrc_decrypted.slt`, ported by `_claude_stage\torana_clean\db\port_fh3_db.py`). v7 result (user): loads and races, only yellow paint, no damage, brake lights are dots. v8: real FH3 paint list (7 colours, `db\fh3_colors.py` + `torana_colors.json`) and damage-morph buffers (cause of no damage: our writer leaves FM4's per-vertex Morph buffers empty; v8 fills them from the W427 by nearest vertex, `torana_clean\add_morph.py`). v8 result: colours and damage work. v9: the Torana's own FH3 damage data (deformation good; damaged areas went grey). v10: body UV1 moved off the W427 damage-atlas scratch area (works). v11 (textures, works). v12: FH3 shut-line parts, panel gaps closed (works). v13: merge of own shell, own FH3 wheels/brakes and FOR_XBFalconGT_73 carattribs (`claude/fh3-torana-own-shell-2026-10-12.md`, `claude/fh3-torana-wheels-brakes-2026-10-12.md`); needles OK, damage works. v14: bake fix. v15: cameras, wheel track, dial faces. v16 (success per user). v17 (installed 2026-10-12, awaiting test): four-agent polish (dials, needle glow, mirrors, mips, scratches, interior colours, thumbnail, Falcon 351 audio). v16 contents: grille, decals, own livery masks, cockpit door area (`claude/fh3-torana-grille-decals-2026-10-12.md`). Open: door 'gap' in race cockpit view (not missing geometry), white mirrors, speedo calibration: step 2, the Torana's own texture atlases and textured lamps (`_claude_stage\tex\make_atlas.py`), converted with the decoded FM4 morph format (`torana_clean\fh3_morph.py`, `fm4morph.py`). W427 wheel/brake models and Mustang audio are still borrowed.

Removed from the live install: offers 99 (Monaro) and 100 (Mustang) in `.local\work\holden_cleanup_2026-10-11\`; old offer 101 and offer 70 (FH Shelby) in `.local\work\fh3_offers_removed_2026-10-11\`; RT test cars and T1/T2 in `.local\work\_to_delete_2026-10-11\`.

Next: v9 damage test; damage for FM2/FM3 ports; use the FH3 DB for every FH3 port (rows map by column name); shadow lighting tune (user TODO, later); Torana wheels/brakes, physics, audio, texture detail, gauge needles; move FH3 tools into the repo; then rebuild the Monaro and Mustang with the clean builder.

### D. FH and FH2 cars
- FH DLC cars: offers 71 to 81 installed. Offer 70 (FH Shelby GT500) was an old test mule and has been retired (user, 2026-10-11); its pack stays in `.local\work\fh3_offers_removed_2026-10-11\` only as a backup. Do not use it as a reference car. The Honda pack (71) runs. The others have not been raced.
- LaFerrari (FH2, offer 82, car 1756) loads in menus.
- Tier 1 FH and FH2 base cars: not yet ported.
- Worth checking: FH/FH2 carbins go through `carbin.py`, which keeps the vertex bytes (FH 28-byte vertex: the 4 signed bytes become FM4 words 12-15), so they should carry real ambient values.

### E. Tracks
Not started. Plan in `track-conversion-plan.md`.

## 6. Ids and offers

Full id table: `car-port-handoff.md` section 5. As of 2026-10-11 09:40 UTC:
- 36 packages are mounted (the game mounts at most 37). Plan variants as cars inside one offer, not as new offers.
- Offer 101 (`4D530910F4FF7F00`, string priority 25800): car 9330, wheel 90530, mapping 10130, body 9330000, engine 9380 (torque 9380000+), drivetrain 9480 (all from the FH3 DB).
- Free again: offer 70 (retired FH Shelby test mule) and, since the Holden clean-up, offers 99 and 100 (their old ids 9110-9112, 9210 are unused while those packs stay removed).
- Always check ids against the installed merges and the staged packs before use. Keep CarBodyID == List_UpgradeCarBody.Id == car*1000.
- ContentOffers ID: `4D530910F4FF` + two-digit offer + `00` for offers 70 and up (101 uses `7F`).

## 7. Safe install procedure

1. Confirm with the user that FM4 is closed.
2. Build the pack into a staging folder first. Check the zip members, the merge rows and every id.
3. Back up the live zip and DB. Move the live extracted folder out with `mv -n` into a `_to_delete` folder (deleting is not allowed without approval).
4. Copy the new zip, string tables and extracted folder in (split copies so each call stays under 170 s). Verify CRCs and that the extracted files match the zip.
5. Ask the user to test with the file trace on. Record the result in the thread's status doc.

## 8. Known pitfalls

- A sqlite id clash breaks races silently. Engine and drivetrain bases must stay 100 apart.
- Clones need every Ordinal-keyed row and a ContentOffersMapping row, or the car-select window stays grey.
- A Data_CarBody id that does not equal List_UpgradeCarBody.Id (car*1000) gave a grey preview (suspected; v6 tests the fix).
- Every material of the donor carbin must appear in the output (missing names crashed the Mustang race load). The clean builder adds hidden 1 mm placeholder triangles.
- Vertex words 12-15 must hold real ambient SH values; zero = matte black body.
- UV set 1 must point into the per-car atlas (0..1); tiling UVs belong in UV set 2.
- Empty triangle arrays need shape (0, 3).
- Car names use the 0xCD49 key namespace.
- String tables are cumulative per pack.
- Background jobs die when a device call ends.

## 9. Open questions for the user

1. v17 Torana test (see `claude/global-todo.md`). Next big item: a generic FH3 porter tested on a second car.
2. Which car shows the "VW Jetta type body" (FM3), and in which offer?
3. Results for the nine untested FM3 batch 1 cars.
4. Can the unknown pack at string priorities 25400 and 25500 be removed?
5. May the `_to_delete` folders be deleted?
6. Which FM2, FM3 and FH3 cars come next?

## 10. Suggested next steps for the next agent

1. Get the v17 result (rollback: `roundtrip\backup_v17pre\` zips + `fm4-recomp\.local\torana_backups\101_pri_65_v16_extracted`). Build the pack with `FZLZX=~/tt/libfzlzx.so`.
2. Apply the writer lesson to FM3 and FM2: check what their converters write into vertex words 12-15 and the UV sets (likely cause of the FM3 lighting mismatch).
3. Move the FH3 builder, baker and atlas decoder into `tools/dlc/`.
4. Torana: wheels and brakes, physics rows, audio, texture detail; shadow-lighting tune.
5. Keep `global-todo.md` and this handover current.

## 11. Updating this handover

When you finish a session's work, update section 5, section 6, section 9 and the title time. Write the full file back with `project_write`, and update the repo copy (`AGENT_HANDOVER.md`) and the local copies in `C:\Z-Temp\Forza-4-compiling-stuff\_claude_stage\project-docs\` (not inside fm4-recomp: a recomp rebuild clears `.local\work`).
