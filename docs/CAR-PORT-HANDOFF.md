# Car port handoff: bringing missing cars into Forza Motorsport 4 (FM4 recomp)

> Mirror of the project doc `claude/car-port-handoff.md`. Paths starting `claude/` are docs in the claude.ai project "Forza-4-Evolution".

Written 2026-10-10 for an agent resuming cold; revised 2026-10-11 after checking every installed pack on disk. Where something is a guess it says so. The live todo list is `claude/global-todo.md` (also `GLOBAL_TODO.md` in the repo). Per-source detail: `claude/fm3-port-status.md`, `claude/fm2-carbin-format.md`, `claude/fh3-port-status.md`, `claude/fh-car-port.md`.

## 0. Goal and current state

Goal: make FM4 (Xbox 360 game recompiled to run on Windows, repo `fm4-recomp`) carry the cars that earlier Forza games have and FM4 lacks, by converting them into FM4 DLC packs.

Confirmed by the user:
- 40 FH DLC cars installed (GT500, 3 Honda Challenge cars, 36 more). All load in menus. The GT500 and Honda pack also run in a race. The other 36 have not been raced.
- LaFerrari (FH2 base car, offer 82) loads and starts a race, after the dangling `AeroPhysicsID` fix (`claude/fh2-laferrari-race-hang.md`).
- FM3 Aveo5 LT (offer 85, real FM3 body and brakes) loads, drives and sits correctly in a race. Brakes not visible in game; deferred.
- FM3 batch 1 (offer 84, 11 cars): Boxster S and Monte Carlo SS Stock Car load, run and drive. Both show a lighting and material mismatch between the preview cameras and the race.
- FM2 Lotus Elise 135R (offer 86): showroom, pre-race preview and race work, and the name shows. Handling feels "like a go-kart".

Installed, no in-game result recorded:
- FH3 Monaro variant test (offer 99, 3 cars) and FH3 Mustang GT500 test (offer 100). See `claude/fh3-port-status.md`.

Done and no longer blocking:
- Base-car mode for FH and FH2 cars: `tools/dlc/make_fh_base_pack.py`, including the foreign-reference copy (`foreign refs copied`).
- FM3 car and vertex converter, including 16-byte brake vertices: `tools/dlc/carbin_fm3.py` and `fm3vert.py`.
- FM3 batch driver: `tools/dlc/batch_fm3.py`. Allocates ids and runs the pack builder and port tool. Foreground only, under 170 s per call.
- FM2 converter: `carbin_fm2.py` (in `Forza-4-compiling-stuff\fm2_converter\`) and pack builder `tools/dlc/make_fm2_base_pack.py`. Name keys rewritten to the 0xCD49 namespace.
- FH3 readers: `burG` container, modelbin meshes and `.swatchbin` textures decoded.

Open, highest first (see `claude/global-todo.md`):
1. Lighting and material mismatch in the race on all FM3 cars tested.
2. Brakes are in the Aveo pack but not visible in game. Deferred by the user.
3. Audio (CMT/ET) for FM3 cars still from the Spark donor.
4. Race-test the 36 FH DLC cars.
5. FM2 batch 1 (offers 87 to 89, staged, not installed), then the rest of the FM2 stock roster.
6. FH Tier 1 base cars 2 to 37, FM3 disc 2 cars, FH3 in-game test, tracks (not started).

Ranked car lists: `claude/port-priority-list.md`.

## 1. Where things are

All on the user's Windows PC (device "sancho"). Two folders:
- Repo: `C:\Users\Me\Documents\GitHub\fm4-recomp` (mounted as `$HOME/mnt/fm4-recomp`)
- Game data: `C:\Z-Temp\Forza-4-compiling-stuff` (mounted as `$HOME/mnt/Forza-4-compiling-stuff`). Access usually needs requesting again in a new session.

Game data under `Forza-4-compiling-stuff`:

| Path | What |
|---|---|
| `extracted\Media` | FM4 base game (`db\gamedb.slt`, `cars\*.zip`, `StringTables\*.zip`) |
| `game-content\FH`, `FH2` | Forza Horizon 1 and 2 base game (`media\db\gamedb.slt`, `Media\Cars\*.zip`) |
| `game-content\FM3U1` | FM3 disc 1 (`Media\DB\gamedb.slt`, `Media\Cars\*.zip`, `Media\Wheels\*.zip`) |
| `game-content\FM3U2\Content\...\4D53084D\00000002\4D53084D0CCF00nn` | FM3 disc 2: STFS packages (13 Asian, 14 European, 15 American, 30/35/40 Ultimate cars, 45/60 tracks) |
| `game-content\FM2` | FM2 base game (`Media\DB\gamedb.slt`, `Media\cars`, `Media\StringTables`) |
| `game-content\DLC-FH`, `DLC-F2`, `DLC-F3`, `DLC-F4` | Raw DLC STFS packages for each game |
| `game-content\DLC-extracted\reports\*.md` | Per-pack analysis reports for FM2 and FM3 DLC |
| `game-content\DLC-extracted\fm4-dlc-merges\*.slt` | The `merge.slt` of every FM4 DLC pack |
| `game-content\DLC-extracted\fm4-latest-strings\EN` | Latest FM4 string tables incl. DLC |
| `game-content\DLC-FH\_FM4-ports` | Staging copies of the ported FH packs |
| `fm2_converter\` | FM2 converter (`carbin_fm2.py`) and its outputs |
| `_claude_stage\mustang\` | FH3 Mustang build scripts and built models |
| FH3 `z1` extract | FH3 source data (Monaro, Mustang); exact path not recorded |

Repo tools in `tools\dlc\`: `port_dlc_car.py`, `carbin.py` (FH and FH2 transcoder), `carbin_fm3.py` and `fm3vert.py` (FM3 converter), `make_fh_base_pack.py` (FH and FH2 base cars), `make_fm3_base_pack.py` (FM3 base cars), `batch_fm3.py` (FM3 batch driver), `make_fm2_base_pack.py` (FM2 base cars), `fh3_grub.py` (FH3 `burG` reader), `forza_zip.py`, `forza_str.py`, `forza_str_write.py`, `stfs_extract.py`, `xex_to_pe.py`, `fzlzx.c`, `build_fzlzx.sh`, `dlc_inspect.py`. None of `tools\dlc\` is committed to git yet.

Installed FM4 DLC lives in the repo at `.local\user\0000000000000000\4D530910\00000002\<package>` plus `Headers\00000002\<package>.header`. Uninstall = move both out (into `_to_delete\`). The game must be closed; a move while it runs was refused.

## 2. Environment gotchas

- Use `device_bash` (a Linux VM on the user's PC) for everything touching game data. Python 3 and sqlite 3.37 are there. The cloud `Bash` cannot see these files.
- Each `device_bash` call is a fresh shell with a limit of about 180 s. Background jobs started with `nohup` or `setsid` die when the call ends. Run jobs in the foreground and keep each under 170 s. A `find` over the whole repo times out (the `.local` build tree is huge); limit the depth.
- Files are mounted at `$HOME/mnt/<folder>`. Scratch in `$HOME` outside `mnt` is invisible to the user and lost with the session. Anything worth keeping goes into the repo or the game-data folder.
- Deleting in connected folders needs the user's approval (`device_request_delete_permission`). Do not delete user files; move them into `_to_delete\` instead.
- Build the LZX library first: `mkdir -p $HOME/fzbuild && sh tools/dlc/build_fzlzx.sh $HOME/fzbuild/libfzlzx.so`, then `export FZLZX=$HOME/fzbuild/libfzlzx.so`.
- Installed ported packs store `merge.slt` and the string tables uncompressed (method 0), so they can be read with Python's `zipfile` and `sqlite3` without the LZX library.
- The device connection can drop mid-session. Re-check with the device info tool before a long job.
- Safety rules from the session instructions still apply: no downloads without asking; do not delete or overwrite the user's files except what the task requires.

## 3. File format cheat sheet

Read `claude/fh-car-port.md` for the full `.carbin` spec and DLC package format. Short version:

**FM4 DLC package** (installed extracted): `Media\<offer>.puboffer`, `Media\DLCZips\zipmount.xml`, `stringtables.xml`, `<offer>_pri_65.zip` (cars, wheels, audio xml, `media\db\patch\<offer>000_merge.slt`, thumbnails), `StringTables_pri_<n>.zip` (ONE unused base table per pack, never Tracks/Environments/ContentOffers/Data_Car: a higher-priority zip replaces a whole table; see `_claude_stage/string_migrate` and HOWTO section 9), loose `<offer>_pri_65\Media\audio\...` sound banks. Zip central-directory extra field 0x1123 holds the absolute data offset. merge.slt is SQLite, page size 1024, schema format 1. `.header` is 0x148 bytes plus a u32 LE license mask.

**Compression:** zips use method 21 (Xbox LZX). `forza_zip.py` reads methods 0, 8, 21. Ported packs store models uncompressed (method 0).

**Model versions:**

| Game | Body model | Notes |
|---|---|---|
| FM4 | `.carbin` v2 | target |
| FH, FH2 | `.carbin` v5 | 22 files per car: 11 normal, 11 `stripped_*` (FH-only, dropped). `carbin.py` converts all 11 normal files. |
| FM3 | `.carbin` v1, inner Model v13 | `carbin_fm3.py`. Body, lod0, cockpit, wheel and brakes convert. FM3 brake carbins have no `_lod0` suffix; the pack builder renames them for FM4. |
| FM2 | `.carbin` v2, single carbin per car, no lod0, no cockpit, wheel carbin v1 | `carbin_fm2.py` converts the body into an FM4 template car (all parts, LOD0 close-up layout). Wheels, textures and emitters still come from the FM4 donor. See `claude/fm2-carbin-format.md`. |
| FH3 | `burG` `.modelbin` per part, `.swatchbin` textures | Decoded, not yet a working converter. See `claude/fh3-port-status.md`. |

**FM3 vertices:** body and lod0 use a 40-byte vertex (converted to FM4's 32-byte). Wheels use 44 bytes. Brakes use 16 bytes, which is the first 16 bytes of the wheel vertex. `fm3vert.convert_vb` handles all three.

**Strings:** `LSB2` tables; `forza_str.py dump <file.str>`. The DB stores `_&<u32>`; the low 16 bits are the hash into the table. Car names need the high 16 bits 0xCD49 (FM2 sources use 0xA949, which FM4 does not resolve). Offer names use 0xB8D0. Latest wins by priority number, and every pack ships the full table, so strings from removed packs live on in later packs' tables.

## 4. How to reproduce the roster comparison

For each source game `G`:
1. `Data_Car` in `G`'s `gamedb.slt` gives `Id, Year, MakeID, DisplayName, MediaName, ClassID`.
2. Names: extract `Data_Car.str` from `<G>\...\StringTables\EN.zip` with `forza_zip.py extract <zip> <out> --match Data_Car`; resolve `DisplayName` low 16 bits against `forza_str.read()`.
3. FM4 "has" set = FM4 base `Data_Car.MediaName` plus every `DLC-extracted\fm4-dlc-merges\*.slt` plus the merge inside every installed pack zip.
4. Missing = source MediaNames not in the "has" set, then a name and year check (±1), because the same car often has a different MediaName. Also check FM4 base `Data_Car` by year and model name: FM4 files some cars under another make (the GT-500KR and 2007 GT500 are `SHE_` cars), and some base cars only have their zips in the disc 2 packages `4d5309100000000{1..4}`.
5. Caveats: name lookups for some FM4 DLC cars resolve to long description text (hash collisions); FM4's base DB lacks the Porsche make (the Porsche DLC adds it).

## 5. How the port tool works (DLC to FM4)

`tools/dlc/port_dlc_car.py` (see `claude/fh-car-port.md` for options and a test log):
- Input: an extracted package folder. It transcodes every `.carbin` under `media/cars` and `media/wheels` (with `carbin.py` for FH; FM3 and FM2 files are already converted by `make_fm3_base_pack.py` / `make_fm2_base_pack.py`), stores them uncompressed, writes DB rows into `media\db\patch\<offer>000_merge.slt`, writes string tables (`Data_Car`, `ContentOffers`), builds `.puboffer` and zip files, and installs with `--install`.
- Options: `--car-id`, `--engine-id`, `--drivetrain-id`, `--mapping-id` are bases (ids go consecutively in source-id order). Also `--offer`, `--offer-id`, `--str-pri`, `--wheel-id`, `--pack-name`, `--display-suffix`, `--fill-car <FM4 Data_Car Id>`, `--report`, `--include-existing`, `--work <local dir>` (required), and the bisect helpers `--model-donor`, `--db-donor-car`, `--db-donor-tables`.
- It skips cars FM4 already has (unless `--include-existing`) and cars whose make FM4 lacks, drops FH-only DB columns, drops career/livery/event tables, clamps unknown family ids and caps PerformanceIndex at FM4's highest.
- Known gap: FH base-game audio XML and sound banks are sometimes needed; the audit logic is not in the repo.

**Race-load hangs.** Any id reference that leaves the car's id range (AeroPhysicsID, AntiSwayPhysicsID, SpringDamperPhysicsID, TorqueCurveID) and has no target row hangs the race load silently. Torque curves are keyed engine×1000. See `claude/fh2-laferrari-race-hang.md` and `claude/fm3-db-port-findings.md`.

**ID allocation.** Use `batch_fm3.py --plan` for FM3, or check the installed merges by hand. Ids already used (do not reuse), checked against the installed packs on 2026-10-11:

| What | Used |
|---|---|
| Car Id / ModelId / Ordinal | 1700 (GT500), 1710 to 1712 (Honda), 1720 to 1755 (FH batch), 1756 (LaFerrari), 1757 (Aveo, removed), 1758 to 1768 (FM3 batch 1), 1769 (Aveo, offer 85), 1770 (Elise, offer 86), 9100 (Monaro test, removed), 9110 to 9112 (Monaro variants, offer 99), 9210 (FH3 Mustang, offer 100). FM2 batch 1 staged ids: not recorded, check the staged packs. |
| Offers | 70 (GT500), 71 (Honda), 72 to 81 (FH batch), 82 (LaFerrari), 83 (Aveo, removed), 84 (FM3 batch 1), 85 (Aveo), 86 (Elise), 87 to 89 (FM2 batch 1, staged, not installed), 95 (Commodore body test, removed), 97 and 98 (Monaro tests, removed), 99 (Monaro variants), 100 (FH3 Mustang) |
| ContentOffers ID | Three schemes are in use. Two-digit decimal text: 70 = `4D530910F4FF7000`, 71, 82, 84, 85, 86 = `...F4FF8600`. Hex of the offer: FH batch 72 to 81 = `...F4FF4800` to `...F4FF5100` (79 = `F4FF4F00`). FH3: 99 = `...F4FF7D00`, 100 = `...F4FF7E00`. A removed Monaro test used `...F4FF7900`. Check any new ID against every installed merge. |
| String priority | 25100 to 25210 (FH), 25220 (LaFerrari), 25230 (Aveo, removed), 25240 (FM3 batch 1), 25250 (Aveo, offer 85), 25310 (Elise; 25260 to 25300 probably used by Elise test builds and FM2 batch 1), 25600 (Monaro, offer 99), 25700 (Mustang, offer 100) |
| Engine ids | 1100, 1120 to 1122, 1130 to 1165 (FH), 1170 (LaFerrari), 1171 (Aveo, removed), 1172 to 1182 (FM3 batch 1), 1183 (Aveo), 1184 (Elise), 9160 to 9162 (Monaro), 9260 (Mustang) |
| Drivetrain ids | 1050, 1070 to 1072, 1080 to 1115 (FH), 1120 (LaFerrari), 1180 (Aveo, removed), 1272 to 1282 (FM3 batch 1), 1283 (Aveo), 1284 (Elise), 9130 to 9132 (Monaro), 9230 (Mustang) |
| Mapping ids | 1900, 1910, 6000 to 6352 (FH), 6400 (LaFerrari), 6410 (Aveo, removed), 6411 to 6421 (FM3 batch 1), 6422 (Aveo), 6423 (Elise), 9910 to 9912 (Monaro), 10010 (Mustang) |
| Wheel ids | 11600 to 11636 (FH), 11700 (LaFerrari), 11800 (Aveo, removed), 11801 to 11811 (FM3 batch 1), 11812 (Aveo), 11813 (Elise), 90300 (Monaro), 90400 (Mustang) |

Engine and drivetrain ids must not overlap: torque curves are keyed engine×1000 and share the owner id space with drivetrains. The batch driver keeps them 100 apart. The removed Aveo (drivetrain 1180) overlapped batch 1's engine 1180, which is why the Aveo was rebuilt at offer 85.

Always verify new ids against FM4 base `gamedb.slt`, all `fm4-dlc-merges`, and all installed port merges before use.

## 6. The plan, in order

Read `claude/port-priority-list.md` for the full ranked car lists and `claude/global-todo.md` for what is open.

**Done:** base-car mode for FH and FH2; FM3 converter for body, lod0, cockpit, wheels and brakes; FM3 pack builder and batch driver; FM3 batch 1 (11 cars); FM2 converter and the Elise; FH3 format decoding.

**Next:**
1. Fix the lighting and material mismatch in the race on FM3 cars. Compare FM3 and FM4 material and shader data for one car.
2. Brakes: converted, but not visible in game. Deferred by the user.
3. FM3 audio (CMT/ET) instead of the Spark donor.
4. Race-test the 36 FH DLC cars.
5. FM2: install and test batch 1 (offers 87 to 89), then the rest of Tier 3.
6. Port FH Tier 1 base cars (2 to 37) in packs, after one test car. Check logos, family rows and foreign id references.
7. FM3 disc 2 cars: extract STFS packages with `stfs_extract.py`, then run the batch driver.
8. FH3: get in-game results for offers 99 and 100. See `claude/fh3-port-status.md`.
9. Tracks: not started. See `claude/track-conversion-plan.md`.

Blocked until a new manufacturer exists in FM4: Eagle (Speedster), Bowler (Nemesis), Proton (Spirra), Bus, Kenworth, PG.

## 7. Where the cars are (source locations)

- FH: `game-content\FH\Media\Cars\<MEDIANAME>.zip`
- FH2: `game-content\FH2\Media\Cars\<MEDIANAME>.zip`
- FM3 disc 1: `game-content\FM3U1\Media\Cars\<MEDIANAME>.zip` (wheels in `Media\Wheels`)
- FM3 disc 2: inside `0nnn_pri_65.zip` of each STFS package (`stfs_extract.py <package> <outdir>`)
- FM2: `game-content\FM2\Media\cars\<MEDIANAME>.zip`
- FH3: the `z1` extract (part bundles under `scene/exterior/` and `scene/_library/`)

## 8. Risks and open questions

- Lighting and material mismatch on FM3 cars. Cause not yet identified.
- Brakes are converted but not visible in game. Cause not yet identified.
- In-race loading of the 36 batch-ported FH DLC cars is untested.
- FH2 base cars are only proven on one car (LaFerrari); others may hit shader, model-version or foreign-reference problems.
- FM2 cars borrow wheels, textures and emitters from an FM4 donor; physics are FM2-mapped with donor springs and dampers.
- FH3 needs material and UV mapping and a wheel solution before a real port.
- Unknown: the dropped FH car-root records, FH shader/fx names that FM4 might lack, and Model16 v17 semantics.
- Wheels or hitbox wrong on a port: check `Data_CarBody` first. FM4 reads ModelWheelbase, track, ride heights and PristineBoundingBox from it, not from `maxdata.xml` (see `claude/carbin-garage-reference.md`).
- Name-based "missing" results are judgement for different-trim cars of models FM4 already has. Ask the user if unsure.
- The FM3 Audi A4 Touring has no `carattribs.xml` in its zip and is skipped by the batch driver.

## 9. Housekeeping

- `.ok` marker files from `stfs_extract.py` sit in `Extracted` folders under `DLC-FH`; safe to delete.
- `_to_delete\` in the repo holds old Aveo packs, three superseded Elise builds, the Monaro and Commodore test packs (offers 95, 97, 98 and four offer 99 variants), Rally and 1000 Club junk, and a stray `.bak`. Delete only with the user's approval.
- Update the project docs after each batch. Keep the id table above current.

## 10. Update log

- 2026-10-10: FH base-car mode, LaFerrari test pack (offer 82) confirmed in menus; race-load hang fixed (dangling AeroPhysicsID), LaFerrari races.
- 2026-10-10: FM3 converter for body and vertex re-encode. Aveo with real body confirmed in race (offer 83, then rebuilt as offer 85).
- 2026-10-10: FM3 batch driver. Batch 1 (offer 84, 11 cars) installed. Boxster S and Monte Carlo SS confirmed running.
- 2026-10-10: FM3 16-byte brake vertex decoded and converted. Aveo offer 85 installed with real brakes. Brakes not visible in game; deferred.
- 2026-10-10: FM2 converter. Elise 135R (offer 86) works in showroom, preview and race; name fixed (0xCD49 key). FM2 batch 1 staged as offers 87 to 89.
- 2026-10-10: FH3 modelbin and swatchbin decoded. Monaro test packs (offers 95, 97, 98, 99) and FH3 Mustang sanity check (offer 100) built.
- 2026-10-11: Docs checked against the installed packs; id table extended with offers 86 to 100 and the three ContentOffers ID schemes.
- 2026-10-11: Reviewed the external carbin-garage tool (FM4 and FH1 only). Findings and limits in `claude/carbin-garage-reference.md`; our FM2, FM3, FH2 and FH3 converters remain the only path for those games.
