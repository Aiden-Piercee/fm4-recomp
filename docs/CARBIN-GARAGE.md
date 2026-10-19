# carbin-garage: external FM4/FH1 car tool, what it covers and what we take from it (2026-10-11)

Found by the user on 2026-10-11: https://github.com/ItsNotPaths/carbin-garage (author ItsNotPaths, GPL-3.0-or-later, Nim + SDL3, Linux builds). Read at commit `932b4e7` (2026-07-10, "scale phys fix"). Nothing from it is installed or copied into our repo yet. Our repo is also GPL-3, so code reuse is licence-compatible with attribution.

Credits it lists: Mike Davis (Forza Studio), Warshack (UDLC, DLC structure), SoulBrix (FM4 exporter).

## 1. What it actually supports (read this first)

It is an **FM4 and FH1 only** tool. It does not convert FM2, FM3, FH2 or FH3 cars.

| Game | carbin-garage status |
|---|---|
| FM4 (TypeId 1/2/3, "cvFour") | Full parse, byte-equal round trip, glTF export, re-emit |
| FH1 (TypeId 5, "cvFive") | Full parse incl. lod0, cockpit, calipers, rotors; byte-equal round trip |
| FM3 ("cvThree") | Version detection only (Forza Studio's GetVersion rule). Parser accepts it only because some FH1 brake carbins trip the FM3 heuristic. No FM3 vertex/section support. |
| FM2 ("cvTwo") | Detection only. Parser comment: "FH2/FM2/FM3 stubs are still future work". |
| FH2, FH3 | Not supported (ForzaTech notes in its docs are hypotheses only). |

So our own converters stay the only path for FM2 (`carbin_fm2.py`), FM3 (`carbin_fm3.py`, `fm3vert.py`), FH2 (`carbin.py` v5) and FH3 (`fh3_grub.py` + clean builder). For FH1 cars, our `carbin.py` / `port_dlc_car.py` already work in FM4; carbin-garage is a second, independent implementation and a reference.

## 2. What it does

- Import an FM4 or FH1 car zip into a working tree: glTF of every carbin (main, lod0, cockpit, 4 calipers, 4 rotors, wheels instanced at the hubs), decoded PNG textures, the car's DB rows (`cardb.json`), `carslot.json`.
- GUI (SDL3): browse game libraries, render the car, edit Data_Car stats, swap parts between donor cars.
- Export back to the same game (byte-equal or edited zip), or port FM4→FH1 and FH1→FM4 as a DLC package for Xenia.
- `patch-xex`: FH1 default.xex integrity-check bypass (not needed for DLC).
- `.xds` texture decode and encode (Xenon BC1/BC3/BC5 incl. `_AS_16_16_16_16` variants, detile/retile, mip chain). 190/190 sample files re-encode with byte-equal size, mean error 0.13/255.
- LZX: reads method 21 via libmspack; writes via a patched wimlib, but only single chunks ≤ 64 KiB. Its zip writer stores edited entries as method 0 (same as our ports do).

CLI: `mount`, `import <zip> --profile fm4|fh1`, `export-to`, `port-to`, `port-to-dlc <working/slug> fm4|fh1 --donor <slug> --content <dir>`, `decode-xds`, `encode-xds`, `reencode-textures`, `dump-cardb`, `patch-xex`, `roundtrip`.

Repo docs worth reading (all in `docs/`): `FM4_CARBIN_MASTER.md` (1900 lines, FM4 carbin), `FM4_CARBIN_CONDENSED.md`, `FH1_CARBIN_MASTER.md`, `FH1_CARBIN_TYPEID5.md`, `CARBIN_TRANSCODE.md`, `FH1_TO_FM4_PORTING.md` (their FH1→FM4 DLC work, most relevant to us), `FH1_PART_EDITING.md`, `FH1_PHYSICSDEFINITION_BIN.md`, `FH1_PHYSICS_DB.md`, `FORZA_LZX_FORMAT.md`, `PLAN_DLC_PIVOT.md`.

## 3. Findings that agree with ours (independent confirmation)

- FH1 vertex is 28 bytes, FM4 is 32; bytes 0..23 are identical (position, UV0, UV1, quaternion normal). Our `fh-car-port.md` says the same.
- FM4 honours a DLC `merge.slt` overlay at `Media/db/patch/<n>00_merge.slt`; page size 1024 and schema format 1 are required (modern SQLite defaults are silently rejected).
- Zip central-directory extra field 0x1123 is required.
- Sub-id tables (Data_CarBody, List_AeroPhysics, List_UpgradeCarBody*) must be cloned with the new car id; dangling ids break loading (our LaFerrari hang).
- The 9 FH-only Data_Car columns are dropped for FM4 (OffRoadEnginePowerScale, OffRoadFront/RearWheelGripScale, OffRoadTCSFullEffectMultiplier, IsCameraCollidable, IsRentable, IsSelectable, Specials, UseBoxCameraCollision), plus the `E3Drivers` table.
- FH-only car files (`stripped_*.carbin`, `physicsdefinition.bin`, `interior_emissive_LOD0.xds`, `zlights*.xds`) are dropped for FM4; `versiondata.xml` comes from the FM4 donor.

## 4. New to us, worth using

1. **FM4 reads loose, uncompressed geometry from the DLC overlay.** Their in-game test (Xenia, 2026-05-28): a DLC with geometry as loose files in `<dlcId>_pri_NN/Media/cars/<NAME>/` next to `Media/db/patch/<dlcId>00_merge.slt` rendered, could be bought and painted. No cars zip at all. Our packs put cars in `<offer>_pri_65.zip`. If this holds in the recomp, test iterations would need no zip rebuild. **Untested in our build.**
2. **Wheel position, track and hitbox come from `Data_CarBody`, not `physics/maxdata.xml`.** ModelWheelbase, ModelFrontTrackOuter, ModelRearTrackOuter, ride heights and PristineBoundingBox (the hitbox) are read from the DB row at runtime; shipping a source maxdata.xml changed nothing in-game. The values are re-baked versus the XML (sign flips, adjusted track), so deriving them from MAXData is wrong. Relevant to every port where wheels sit wrong in the arches (FM2, FM3, FH3 Torana).
3. **`CarPartPositions` (Ordinal-keyed) holds part and light anchor points.** They carry it from the source car with the id rewritten. Our Torana v6 already touches this table; their result supports keeping the source rows when the source game has them (FH1, FM4).
4. **Part bounds (9 floats per section) are byte-identical between FM4 and FH1 builds of the same car**, and the runtime honours a written bound: a bigger bound makes the part bigger. The engine appears to recentre a part on its bound.
5. **Part limits (FH1 runtime, likely the same engine logic in FM4, unproven there):** sections are keyed by name against a fixed part-name registry (`body`, `hooda`, `bumperF{a-d}`, `wing*`, `skirt*`, `glass*`, `headlight{L,R}`, `seat{L,R}`, `steering_wheel` ...). An unknown name crashes (AV at 0x20000000). More sections than the donor has always crashes (fixed allocation sized from the donor). Fewer sections is fine. Replacing a slot works if the vertex count is ≤ the donor slot's. Glass sections carry permutation blocks and are not safe templates.
6. **Material behaviour seen in-game (FH1):** a cloned `interior` material is unlit (black) in race but textured in autoshow/LOD0; `emblem` is alpha-masked; `reflector` (lights atlas) is emissive. Same preview-versus-race pattern as our FM3 lighting issue, so material choice is a second suspect after vertex words 12-15.
7. **Mixed-case file names**: geometry matching must be case-insensitive (FH ships mixed case). We already handle this.
8. **`physicsdefinition.bin` (FH1) layout:** version 1, count 13, epsilon 0.01, two symmetric 3×3 inertia tensors (unit mass at 0x10, mass-weighted at 0x34), a scalar block at 0x58, 44-byte attachment records from 0xCC (wheel/suspension pivots, mirror-pair flag), then convex collision hulls, fixed 10-byte footer. FM4 does not load this file. Useful as a starting point for the FH3 physics decode, though FH3 is ForzaTech and may differ.
9. **Donor fingerprinting:** they identified an unknown donor by matching its cloned CarPartPositions rows against the base gamedb, and ranked donors by wheelbase from each zip's `maxdata.xml`. Same idea works for our donor choice.
10. **Texture tool:** `core/xds.nim` is a tested Xenon `.xds` encoder. Candidate for the Torana texture-detail work and FM2 textures, if we build it (Nim ≥ 2.2; the cloud container can build it).

## 5. Where we are ahead (possible upstream contribution)

- **FM4 vertex bytes 24..31 (their "extra8", "not decoded, suspected second tangent").** We decoded it on 2026-10-11: four s16 values of baked per-vertex ambient light (order-1 SH, s16/32767, order L0, y, z, x). Zero gives a matte black body. See `claude/converter-writer-test-step1-2026-10-11.md`.
- **FH1 bytes 24..27 (their "extra4", "byte 0 matches FM4 ~70%").** Our `carbin.py` treats them as the same four values as s8 (about s16 >> 8) and expands each with `b << 8`, and the FH ports render correctly. carbin-garage's FH1→FM4 path instead copies the 4 bytes raw and zero-pads the last 4, which scrambles the lighting words. That is a likely cause of the "minor visual issues" they report on FH1→FM4 ports. **Do not adopt their reverse vertex transcode as is.**
- FM3, FM2 and FH3 converters, which they do not have.

If the user wants, these two points could be sent upstream as a GitHub issue. Not done; ask first.

## 6. How to use it here

- As a reference: read their docs before touching FM4 carbin structure, DLC layout or Data_CarBody. Cite file names from `docs/` above.
- As a validator (not set up yet): build it in the cloud container and run `import` on our converted FM4 cars to check that an independent parser reads them cleanly and to view them as glTF.
- Not as a replacement for our FM4 install path: its FM4 output targets Xenia's content layout (`_pri_99`, `<dlcId>00_merge.slt`, ContentOffers ids from `0x4D53091000000001 + dlcId`), while ours uses `_pri_65`, `<offer>000_merge.slt` and `4D530910F4FF..00` ids. Do not mix the two schemes in one install.

## 7. Follow-up tasks (also in `claude/global-todo.md`)

- [ ] Test loose geometry in the recomp: one installed car, carbins moved from the zip into the `_pri_65` overlay folder.
- [ ] Check Data_CarBody (wheelbase, tracks, ride heights, PristineBoundingBox) on the FM2, FM3 and FH3 ports against the source car's real values.
- [ ] Check material choice as a second cause of the FM3 preview/race lighting mismatch.
- [ ] Optional: build carbin-garage in the cloud and use it as a second parser on our outputs.
- [ ] Optional: report the extra8 / extra4 findings upstream (user's call).
