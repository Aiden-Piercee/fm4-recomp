# Current status

Last validated: 2026-09-03 on Windows AMD64 with D3D12. Later development
results (2026-10) are listed under "Experimental" and do not change the
validated profile.

## Working

| Area | Validated result |
|---|---|
| Startup | The title reaches the main menu and loads the tested profile. |
| Career | The mandatory opening race completes. Saving, achievements, and driver-level progression work. The tested save progressed through Amateur and Clubman into Sportsman. |
| Modules | `XMediaFacade_default.xex` and `SpeechFacade_default.xex` load as recompiled modules. |
| Disc 2 | The four FM4 marketplace packages install into user data. Installed cars appear in the browser and the menu no longer offers Disc 2 installation. |
| Post-launch DLC (2026-10-09) | The 16 owned FM4 marketplace DLC packages install from a DLC folder in one pass (`16 installed, 0 already present, 0 rejected`) and their cars load. Track packs, the Porsche Expansion and packs that may expect title-update code have not been played through. |
| Graphics | The validated native-resolution profile renders the tested player-car shadow, car materials, road lighting, and trackside assets correctly. |
| Local release build | The builder-first path validates the three XEX inputs, builds SDK commit `f16992c`, generates all three modules in isolated local state, and completes all 293 FM4 build steps. The resulting executable passed startup smoke tests and an operator launch through first-profile setup and car selection. |

## Validated graphics profile

```toml
resolution_scale = 1
render_target_path_d3d12 = "rov"
readback_resolve = "full"
d3d12_readback_resolve_host_copy = true
```

Use the default guest vblank rate. The validated GPU and runtime were built
from the SDK fork at
[`f16992c`](https://github.com/Alexbeav/rexglue-sdk/commit/f16992c92e610c2d71773994436b442d9946f4e1).

## Known issues

- Car-thumbnail generation is under investigation. A fresh isolated profile
  generated a highly compressible blank thumbnail: the showroom background
  rendered, but the car was missing. Blank results can persist in the
  profile's thumbnail cache.
- Full resolve readback is required for the current visual-correctness
  baseline and has substantial, hardware-dependent synchronization cost. The
  validated local build exhibited stutter and about 30 FPS while the host GPU
  was only partly utilized. Performance and remaining visual issues are still
  being improved.

## Build and validation boundaries

- Fresh Windows builds require one consistent C++ ABI. The SDK and FM4 now
  build successfully with LLVM 22 targeting the MSVC ABI after adding a
  feature-gated fallback for standard libraries that do not provide C++20
  `clock_time_conversion` and `clock_cast`. Do not mix an MSVC-ABI SDK with a
  MinGW/libc++ FM4 build; C++ runtime symbols will not link across that boundary.
- Campaign progression is known good through Amateur and Clubman and into the
  Sportsman tier. The campaign beyond Sportsman has not been validated.
- A 2x internal resolution test produced stained-glass texture corruption on the
  validated ROV path. The experimental native path renders 2x correctly (see
  below).
- A 120 Hz guest-vblank override improved throughput but reintroduced screen
  flicker and player-car texture corruption.
- Automatic readback produced higher frame rates, but skipped resolves needed
  by FM4 and caused flickering, texture corruption, and heavy dips.
- Online services and downloadable marketplace acquisition are outside the
  current test scope.
- The `v0.1.0-alpha.1` release is source-only. Users must supply the three
  validated executable inputs from their own legally obtained game copy and
  build locally; no game data, generated translation, or compiled executable
  is distributed.

The graphics configuration is a validated combination, not a fully isolated
root-cause fix. Performance work must retain this profile as its correctness
reference.

## Experimental (not part of the validated profile)

- **Native D3D12 render path** (`render_target_path_d3d12 = "native"`, SDK
  patch 0002). In the Free Play test race it matches the ROV baseline visually
  and averages 47.6 FPS at 1x, against 26.7 FPS on ROV. 2x renders correctly
  and runs at about 30-36 FPS in races; 3x renders but is GPU bound. Full
  resolve readback is still required. Details and run numbers:
  [Native D3D12 renderer](NATIVE_RENDERER.md).
- **Deferred readback sync** (SDK patch 0001, `Launch FM4 (Fast Readback).bat`).
  Waits for resolve copies once per guest sync point instead of after every
  resolve.
- **Cars from other Forza titles.** Tools under `tools/dlc` build FM4 DLC
  packages from the user's own copies of Forza Horizon 1 to 3 and Forza
  Motorsport 2 and 3. Ported cars from FH, FH2, FM2 and FM3 load and race in the
  tested cases; FM3 cars show a lighting and material mismatch in races, and
  FH3 support is at the test-pack stage. Status:
  [Car port handoff](CAR-PORT-HANDOFF.md) and [global todo](../GLOBAL_TODO.md).
