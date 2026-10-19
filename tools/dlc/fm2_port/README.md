# FM2 -> FM4 port framework (step 1: read-only planning)

Status: planning and verification only. Nothing here writes into the game or the repo (except an optional `--report` JSON).

## Files
- `inventory.py` - reads every id, offer, string priority and media name in use: FM4 base gamedb, every installed DLC merge, and staged pack folders. `allocate(inv, k)` gives consecutive ids for k cars; `pick_offer(inv)` gives the highest free offer number <= 99 whose ContentOffers id is unused.
- `donor.py` - ranks FM4 cars as donors for an FM2 car (model similarity, same make, same class, year and performance-index distance).
- `port_fm2_car.py` - entry point.
  - `--car <FM2 MediaName> [--donor <FM4 MediaName>] [--report out.json]`: PASS / WARN / FAIL checks, exit 1 on FAIL.
  - `--roster`: status of every FM2 car (`in-fm4-base`, `in-pack-installed-or-staged`, `make-missing-in-fm4`, `ready`, `ready-weak-donor`).

## Id rules encoded (from docs/CAR-PORT-HANDOFF.md)
- Car, wheel and mapping ids: consecutive runs above the highest id in use. Nothing is reused.
- Engine E and drivetrain E+100 for the same car. Torque curves are keyed engine x 1000, so an engine owner id must be free in the torque keys too.
- Offer numbers: highest free number <= 99. The ContentOffers text id is `4D530910F4FF%02d00`. ContentOffers.ID holds that hex text, OfferId holds the same value as a 64-bit int, and PubOfferingID is the offer number.
- String priority: highest in use + 10.

## Open items (next steps, in order)
1. Id injection in the builder. `make_fm2_base_pack.py` still writes donor ids and a fixed `9000_pri_65.zip` / `9000_merge.slt`. The allocator output must be applied to the mini DB (Data_Car, Data_Engine, Data_Drivetrain, List_Wheels, ContentOffers, ContentOffersMapping, torque keys) and to the pack names, and Elise output must stay byte-identical. The FH path (`port_dlc_car.py`) already does id remapping; reuse it.
2. Alias table `fm2_part_alias.json` for part names (taillightL/R vs glassLTL/RTL, exhaustL/R vs exhausta, bumperRrace vs EXCLUDE_bumperRrace).
3. Verification of the built pack (zip member check, merge row check, race-load check of ids that leave the car range) and an explicit `--install` with the game-closed confirmation and backup to `_to_delete\`.
4. `batch_fm3.py --plan` offer-id bug: it proposes offer 101 (invalid 17-char id). Replace its offer choice with `inventory.pick_offer`.

## Known data points (2026-10-11 run)
- Installed offers: 1-4, 21-39, 70-86, 99, 100. Staged batch: 87-89. Offer 98 is the next free offer under the 2-digit scheme.
- Largest ids in use: car 9210, engine 9260, drivetrain 9230, mapping 10010, wheel 90400. Next pack would start at car 9211, engine 9261 (drivetrain 9361), mapping 10011, wheel 90401, string priority 25710.
- Roster run: 188 cars already in FM4 base, 19 in an installed or staged pack, 13 with no FM4 make, 89 ready, 1 ready with a weak donor.
- The string priorities 25400 and 25500 mentioned in the handover are not in the scan. Their pack folder was not found under the scanned roots; identify it before removing anything.
