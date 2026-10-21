# Engine builder (Forza-4-Evolution)

Build an engine from individual parts (heads, cam, valvetrain, pistons, rotating assembly, intake, carburettor, headers, exhaust, ignition) and drive the result. A build-time engine model turns the parts into a torque curve; in the race the player car's torque is the car's stock FM4 curve multiplied by `model(build) / model(stock build)`.

Status: first playable (MVP). Pilot cars are FM4's carbureted American V8s from 1957 to 1979, plus the 1980s-90s pushrod V8s. Untested in game until B builds it.

## Cleanroom rules

This feature was inspired by how Motor City Online (EA, 2001) built engines from parts. None of that game's code, constant tables or database rows are in this repository, and none may be added:

* The engine model in `fm4/src/engine_build/dyno.cpp` is written from the behaviour description below and from public engine theory (Heywood, *Internal Combustion Engine Fundamentals*; Taylor, *The Internal Combustion Engine in Theory and Practice*; common hot-rodding practice). Its constants are our own.
* The parts in `assets/engine_builder/fm4_engine_parts.tsv` are authored for this project. Brands are fictional. Specs (duration, lift, valve size, cfm) are ordinary real-world figures for that kind of part.
* Engine geometry (bore, stroke, valve sizes) for FM4's engines comes from public manufacturer specifications.

## How it works in game

| Piece | File | What it does |
|---|---|---|
| Engine model | `fm4/src/engine_build/dyno.cpp` | geometry + parts -> torque curve, valve float, safe rpm |
| Catalogue | `fm4/src/engine_build/catalogue.cpp`, `assets/engine_builder/fm4_engine_parts.tsv` | stock geometry per FM4 `EngineID`, the parts, which engine families each part fits |
| Builder state | `fm4/src/engine_build/engine_builder.cpp` | the player car's engine and fitted parts, saved per garage car in `{P}Evolution_EngineBuild`; the torque ratio and rev limit the physics hook asks for |
| Overlay | `fm4/src/engine_build/builder_ui.cpp` | F7: slot list, part list, live dyno graph of the build against stock |
| Physics | `part_tune.cpp` / `overheat.cpp` | `PartTuneTorqueMult` multiplies in `EngineBuilderTorqueRatio`; `OverheatEngineStep` raises the rev limiter to the build's safe rpm |

### Torque

`OverheatEngineStep` (0x82304964, every engine substep, about 359 Hz) already rescales the engine power multiplier E+2736 by `PartTuneTorqueMult(car, rpm)`. For the player car with an active build that multiplier also includes

```
ratio(rpm) = Dyno(build, rpm) / Dyno(stock, rpm) / (product of the fitted FM4 engine upgrade TorqueScales)
```

clamped to 0.35..2.2. Dividing out the FM4 upgrade scalars (Valves, Displacement, PistonsCompression, FuelSystem, Ignition, Exhaust, Intake, Manifold, Camshaft peak) means FM4 engine upgrades bought in the stock shop do not stack with the build; the builder overlay says so. Forced induction (FM4 turbo / supercharger levels) still applies on top, unchanged.

### Rev limit

With an active build the rev limiter E+3636 is set to the build's safe rpm minus 100, but only within the stock curve's range (`TorqueCurveMaxRPM` of the fitted camshaft row minus 150). It is never lowered below the stock limiter.

### Where it applies (PI)

FM4's PI and class come from the authored upgrade data, so a build makes them wrong. Until PI is recomputed (a later phase) the build only takes effect:

* outside career events (free play, test drive), and
* in career events only when `fm4_engine_builder_career` is on (default **off**). 

The current event is the word race start stores at 0x82DDB228; an id found in `Events` counts as a career event. The overlay shows whether the build is live for the next race.

### Saving

`{P}Evolution_EngineBuild(GarageId INTEGER, Slot INTEGER, PartId INTEGER, PRIMARY KEY(GarageId, Slot))` in the profile database. No row for a slot = the stock part. A garage car with no rows has no build and drives exactly as stock FM4.

Parts are free in the MVP. Prices and the Evolution economy come later.

## Engine model (behaviour spec)

Quasi-steady, one cycle averaged per rpm point. Inputs are `eb::EngineSpec` (`dyno.h`).

1. **Swept volume** from bore, stroke and cylinder count. Mean piston speed = 2 x stroke x rpm / 60.
2. **Airflow (volumetric efficiency)**. The engine breathes as well as its narrowest restriction allows at that rpm:
   * *Valve and port.* Effective intake flow area grows with valve diameter, valves per cylinder, lift (saturating at about a quarter of the valve diameter) and port efficiency. Required flow grows with rpm and swept volume. As the ratio of required to available flow passes a choking point (an inlet Mach index around 0.5), VE falls off.
   * *Cam timing.* Longer duration and later intake closing move the VE peak to higher rpm and cost torque at low rpm (reversion); short duration fills well low down and chokes high. More overlap (from duration and a tighter lobe separation) helps only when the exhaust scavenges well (headers) and costs idle quality and low-rpm torque.
   * *Intake tuning.* Each manifold style has a tuned rpm band set by runner length: dual plane broad and low, single plane higher, tunnel ram highest and narrowest, long-runner EFI broad mid-range, log manifolds poor. A tuned runner adds a few percent VE around its band.
   * *Carburettor.* A carb has a rated airflow. When the engine's demand approaches it, VE is capped and torque falls at high rpm. An oversized carb on a small engine loses low-rpm torque (weak signal, poorer mixture).
   * *Exhaust.* Headers with the right primary length add a scavenging gain around their tuned rpm; log manifolds add backpressure. Pipe diameter and muffler restriction cost torque that grows with flow (so mostly at high rpm). Single exhaust on a big engine is a restriction.
   * *Air filter* restriction costs a little, more at high flow.
3. **Combustion.** Indicated efficiency follows the Otto relation from the compression ratio, scaled to realistic engines. A knock limit set by the fuel's octane: compression above it costs timing and so torque (`knock_margin` < 0). Ignition: points lose a little at high rpm (weak spark), electronic is neutral, capacitive discharge holds spark to the top.
4. **Friction.** Friction mean effective pressure rises with mean piston speed (and a little with valve spring load for big solid or roller cams).
5. **Valvetrain limit.** Valve float rpm depends on lifter style (hydraulic flat < solid flat < hydraulic roller < solid roller), lift and rocker ratio (more lift lowers it), unless a springs part sets it. Above it torque falls steeply.
6. **Torque** = (VE x combustion efficiency x fuel energy per cycle) - friction, per rpm point. `safe_rpm` = min(valve float, rod limit).

The model must be smooth (no steps between rpm points) and stable for any input in the catalogue's ranges.

## Catalogue format (`fm4_engine_parts.tsv`)

UTF-8, tab-separated, `#` comments. Looked up in the user data root, then the working directory (same as the rim catalogue).

* `F <family> <display name>`: an engine family (block design), for example `CHEVY_BB`.
* `E <EngineID> <family> <display name> <key=value ...>`: stock geometry of an FM4 engine. Keys are `EngineSpec` fields (see below).
* `P <PartId> <slot> <families> <brand> <name> <year> <price> <mass_kg> <key=value ...>`: a part. `families` is a comma list or `*`. `year` is the first model year it is on sale. Modifiers: `key=value` sets, `key+=value` adds, `key*=value` multiplies.

Slots: `heads`, `cam`, `valvetrain`, `pistons`, `rotating`, `intake`, `carb`, `headers`, `exhaust`, `ignition`.

Keys: `bore stroke cyl rod cr rmass rodrpm vpc ivd evd port portcc idur edur ilift elift lsa icl lifter rocker float intake runner cfm carb filter header pdia plen pipe duals muffler ign octane`. Enum values are the numbers in `dyno.h`.

## Testing

* Host: `tools/engine_build/dyno_test.cpp` (any C++17 compiler: `c++ -std=c++17 -O2 -I fm4/src tools/engine_build/dyno_test.cpp fm4/src/engine_build/dyno.cpp fm4/src/engine_build/catalogue.cpp -o dyno_test`). Checks smoothness, the direction of each part's effect and catalogue parsing.
* Calibration against FM4: `tools/engine_build/dyno_check.py <gamedb.slt>` runs the model over every `E` row and prints peak torque rpm and curve shape against the car's stock FM4 curve. It reads the user's own game database and writes nothing.
* In game: pick a pilot car (1969 Camaro Z28, 1970 Chevelle SS, 1969 Charger R/T), F7, fit a big cam and single-plane intake, take it to Test Drive. `fm4.log` lines start with `Engine builder:`.
