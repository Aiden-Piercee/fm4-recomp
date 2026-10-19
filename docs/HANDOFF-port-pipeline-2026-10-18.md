# Handoff: FM2 / FM3 / FH1 / FH2 -> FM4 port pipeline (2026-10-18)

Written for a terminal Claude Code session to read cold and continue. Source: project docs only (newest port docs are 2026-10-11 to 2026-10-16). Nothing here was checked against the repo or PC on 2026-10-18, so verify file state before trusting it.

## 0. How to start
1. Read `claude/00-project-rules-read-first.md` (project doc) before changing any game data or packs.
2. Read `claude/00-knowledgebase.md` (lessons; add nuance-only entries when you find new ones).
3. Other agents may edit the same code: draft locally, get permission before editing main, never overwrite prior work. Rerun Damage Plus after installing or rebuilding any car pack.
4. Heavy or long jobs: append a `[JOB <id>]` entry to `C:\Z-Temp\Forza-4-compiling-stuff\_claude_stage\HEAVY_LIFTER_QUEUE.md` (BUILD ONLY or INSTALL OK). Never install into the game unless the user says so and the game is closed.
5. The user prefers plain prose, short updates, tasks created when checking/researching/planning, and occasional progress comments.

## 1. Question the user asked
Status of FM2 / FM3 / FH1 / FH2 -> FM4 porting; is the pipeline fully automated; if not, why and how to fix.

## 2. Answer: not automated. No clean one-command path exists.

| Source | Works | Missing |
|---|---|---|
| FH1 / FH2 | `carbin.py` converts 11/11 carbins on both test cars. `make_fh_base_pack.py` + `port_dlc_car.py` remap ids and build packs. 36 FH DLC cars installed; LaFerrari (offer 82) races. | Only GT500, Honda pack, LaFerrari race-tested. 36 FH cars untested in a race. Tier 1 roster (37 cars, 2-37) not ported; may hit new shader/model-version/foreign-reference problems. |
| FM3 | Converter does body, lod0, cockpit, wheels, brakes. `batch_fm3.py` made batch 1 (offer 84, 11 cars) + Aveo (offer 85). Boxster S, Monte Carlo SS, Aveo confirmed driving. | Lighting/material mismatch preview vs race on ALL FM3 cars (no doc records a fix). Audio from Spark donor. Brakes invisible in game. 9 batch-1 cars menu-checked only. Disc 2 cars need `stfs_extract.py` first. Audi A4 Touring skipped (no carattribs.xml). |
| FM2 | Elise 135R works (offer 86). Exige/Tuscan/Evo built, staged in `DLC-FH\_FM4-ports\fm2_batch1\` (offers 87-89), not installed. Planner `tools/dlc/fm2_port/` (`port_fm2_car.py --car X` / `--roster`) says 89 of 311 FM2 cars ready. | Wheels (FM2 wheel carbin v1, no LOD0, no converter), textures, audio, emitters, damage morphs all from donor/empty. Id injection not implemented. Elise drives "like a go-kart" (donor springs vs FM2 damper scale unverified). |

## 3. Why it is not automated (root causes)
1. **FM2 builder ignores allocated ids.** `make_fm2_base_pack.py` writes the donor's ids and a fixed `9000_pri_65.zip`; the planner's allocator output is not applied. Blocks one-command FM2.
2. **FM3 lighting bug is probably the same as the FH3 one.** FM4 car vertices carry baked per-vertex ambient lighting in words 12-15 (order-1 SH, s16/32767, order L0,y,z,x). Zero = matte black; constant = dull. Plus two UV sets (set 1 = atlas, set 2 = tiling). The FM3 converter has NOT been checked this way. FM2 vertex bytes 28-59 also unchecked for the same thing.
3. **No automated gate.** No per-build check for ids leaving the car's range (AeroPhysicsID, AntiSwayPhysicsID, SpringDamperPhysicsID, TorqueCurveID without a target row hang the race silently). No `--install` with game-closed confirmation + backup. The preflight (`claude/conversion-preflight-plan-2026-10-16.md`) is planned, not built.
4. **FM2 has no wheel/texture/audio converters.** FM2 audio is FSB3 XMA; FM4 uses FSB4. Untested whether the recomp's FMOD plays FSB3 as-is.
5. **Small blockers.** `batch_fm3.py --plan` proposes offer 101 (invalid id; use `inventory.pick_offer`). Offer scheme `4D530910F4FF%02d00` runs out after 98. 13 FM2 cars have no FM4 make. Part-name alias table (`fm2_part_alias.json`) not built (taillightL/R vs glassLTL/RTL, exhaustL/R vs exhausta, bumperRrace vs EXCLUDE_bumperRrace; some cars lack glassLHL/RHL, trunk, winga). Weak donors (CHR_ME412_05 -> BMW M5 E39; "make"-tier donors) need a real-car check.
6. **Tooling scattered.** `tools/dlc` unchanged since 13 Oct and untracked in git. Many builders only in `_claude_stage` (809 py files, many versioned copies). Id table in `docs/CAR-PORT-HANDOFF.md` is dated 2026-10-11, missing offers since 90 and cars 1780, 9380, 9400.
7. **Test pack collisions** (from the 16 Oct recheck): 13 Bathurst + 14 Albert Park test packages installed in parallel, shared offer-name hash resolves to "Albert Park Circuit" for many packs. Relevant when picking offer ids and string priorities.

## 4. Recommended order
1. **Check FM3 converter output** (vertex words 12-15 and both UV sets) against a native FM4 car, same method as `claude/converter-writer-test-step1-2026-10-11.md`. Baker (`_claude_stage\roundtrip\bake\`: `rt.c`, `bake.py`, `calib*.py`, `make_baked.py`, `calibration_w427.json`) needs gcc+numpy+scipy, runs in the cloud container, not the device VM. Highest priority; affects every FM3 car and likely FM2.
2. **FM2 id injection** in `make_fm2_base_pack.py`: apply allocator ids to the mini DB (Data_Car, Data_Engine, Data_Drivetrain, List_Wheels, ContentOffers, ContentOffersMapping, torque keys) and pack names. Reuse `port_dlc_car.py` remapping. Elise output must stay byte-identical.
3. **Preflight gate + explicit install**: zip member check, merge row check, out-of-range id check, then `--install` only with game closed and backups to `_to_delete\`. Build the part alias table.
4. **Race-test**: 36 FH DLC cars, 9 FM3 batch-1 cars, FM2 batch 1 (install offers 87-89 only with user OK, then rerun Damage Plus). Then batch FH Tier 1 (test car first, Huracan is next in the list).
5. **FM2 wheel/texture/audio converters.** Biggest payoff: ~56 unique FM2 rims as one pack (`claude/unique-rims-fm2-fm3-fh1-fh2-research-2026-10-12.md`; proof case FM3 `wel_alumastar_2.0_drag`). Apply the morph + static-lip fixes from the knowledgebase so ported rims scale to the car's rim size. Audio: first test FSB3 playback.
6. **Consolidate**: move builders into one repo library (`fm4pack`), commit `tools/dlc`, update the id table and project docs after each batch.

## 5. Key technical facts to remember
- Ids: engine and drivetrain ids must not overlap (torque curves keyed engine x 1000, share owner id space); batch driver keeps them 100 apart. Verify new ids against base `gamedb.slt`, all `fm4-dlc-merges`, and all installed port merges.
- Car-name string keys must be in the 0xCD49 namespace (FM2's 0xA949 leaves names blank). String tables are cumulative per pack: build against a view that includes staged packs.
- DLC `merge.slt` is `INSERT OR IGNORE` (adds rows only). Changes to existing base rows go through Damage Plus `--db-sql` and a rebuild; never edit loose `gamedb.slt`.
- Next free ids (as of 11 Oct, re-run the planner): car 9211, engine 9261 (drivetrain 9361), mapping 10011, wheel 90401, string priority 25710, offer 98.
- Wheels/hitbox wrong on a port: check `Data_CarBody` first (FM4 reads wheelbase, track, ride heights, bounding box from it).
- Standing user rule: every car needs damage data (`fill_morph.py` via `make_damage_pack.py --morph-fill`).
- External tool `carbin-garage` covers FM4/FH1 only; our own converters remain the only path for FM2, FM3, FH2, FH3.
- Source locations (PC): `game-content\FH|FH2|FM2|FM3U1\Media\Cars\<MEDIANAME>.zip`, FM3 disc 2 inside `0nnn_pri_65.zip` of STFS packages.
- Installed DLC: `fm4-recomp\.local\user\0000000000000000\4D530910\00000002\`. Trace log: `.local\work\fm4\out\build\win-amd64-release\fm4.log`.
- Device shell limits: ~180 s per call, background jobs die with the call; the VM has no pip network. Build the LZX helper once per session: `sh tools/dlc/build_fzlzx.sh $HOME/tt/libfzlzx.so; export FZLZX=$HOME/tt/libfzlzx.so`. Use `git -c core.autocrlf=true --no-optional-locks` in the VM.

## 6. Project docs to read (in `claude/`)
`global-todo.md`, `car-port-handoff.md`, `port-priority-list.md`, `ongoing-agent-handover.md`, `fm2-fm4-ongoing-agent-handover.md`, `fm2-full-conversion-status-2026-10-11.md`, `fm2-to-fm4-status-2026-10-11.md`, `fm2-carbin-format.md`, `converter-writer-test-step1-2026-10-11.md`, `conversion-preflight-plan-2026-10-16.md`, `dlc-unification-recheck-2026-10-16.md`, `unique-rims-fm2-fm3-fh1-fh2-research-2026-10-12.md`, `upgrade-audio-framework-2026-10-12.md`, `carbin-garage-reference.md`, `fm3-port-status.md`.

## 7. First action for the next session
Create a task list, then start with step 1 above (read-only FM3 converter vs native FM4 vertex comparison), report findings, and ask the user before writing anything to main or installing.
