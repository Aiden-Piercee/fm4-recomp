# Forza Motorsport 4 Recompiled

![Forza Motorsport 4 Recompiled hero artwork](assets/fm4-recompiled-hero.png)

An experimental static recompilation of the Xbox 360 version of Forza
Motorsport 4 using [ReXGlue](https://github.com/rexglue/rexglue-sdk).

This repository contains project source, recompilation metadata, and build
configuration. It does not contain game data, Xbox system files, saves, or
compiled executables. You must provide files from your own copy of the game.

The project is licensed under `GPL-3.0-only`. The license applies only to
material this project is entitled to license; it does not grant rights to game
files or locally generated translation. See [License and content
boundary](docs/LEGAL.md).

## Project status

This is an experimental alpha source release. On the tested Windows AMD64
system, the current tree can:

- boot into the game and load an existing profile;
- complete races and save career progress;
- progress from Amateur through Clubman and into the Sportsman tier;
- load the two facade modules used by the game;
- install all four content packages from an extracted Disc 2; and
- render the tested races without the rectangular player shadow, corrupt car
  materials, or oscillating road brightness.

The validated graphics profile is intentionally conservative. It uses native
resolution, the D3D12 ROV path, full resolve readback, synchronous host-copy
completion, and the default guest vblank rate. Full readback is expensive, so
it can stutter and may run at about 30 FPS even when the host GPU is not fully
utilized. Performance and remaining visual issues are still being improved.
Higher internal resolution and timing overrides are not considered safe.

## Known issues and tested boundary

- Car-thumbnail generation is under investigation. A fresh profile can save a
  blank thumbnail in which the showroom background is visible but the car is
  missing; that result can then remain in the profile's thumbnail cache.
- The correctness-oriented full-readback path has substantial,
  hardware-dependent synchronization cost. Stutter, low host-GPU utilization,
  and frame rates around 30 FPS are expected on some workloads while this path
  is improved.

Campaign progression is known good through entry into the Sportsman tier. The
campaign beyond that point has not been validated and should not be treated as
confirmed working.

See [Current status](docs/STATUS.md) for the tested boundary and known issues.

## Requirements

- Windows AMD64
- Git, CMake 3.25 or newer, Ninja, and Clang
- a legally obtained, extracted Forza Motorsport 4 game tree

The current release builder uses the project SDK fork at commit
[`f16992c`](https://github.com/Alexbeav/rexglue-sdk/commit/f16992c92e610c2d71773994436b442d9946f4e1).
That commit contains the validated graphics baseline plus Windows toolchain
compatibility work which has not all been accepted upstream. The local builder
obtains and builds the exact pinned source automatically.

The project expects these game executables:

```text
extracted/default.xex
extracted/XMediaFacade_default.xex
extracted/SpeechFacade_default.xex
```

## Local build

The supported release path verifies the required Xbox executables, builds the
pinned SDK, generates translation on your computer, and builds FM4:

```powershell
pwsh -File .\tools\Build-Local.ps1 -GameDataRoot "D:\Games\FM4-extracted"
pwsh -File .\tools\Run-Local.ps1 -GameDataRoot "D:\Games\FM4-extracted"
```

Use `-ValidateOnly` on the build command to check the dump and toolchain
without downloading or building the SDK. Local dependencies, generated code,
and build outputs stay under `.local`; the retained development build is not
modified. The run script also isolates profiles, saves, and caches under
`.local/user`.

## Developer build

From the repository root:

```powershell
Push-Location .\fm4
..\sdk\win-amd64\bin\rexglue.exe codegen .\fm4_manifest.toml
cmake --preset win-amd64-release
cmake --build --preset win-amd64-release --parallel 2
Pop-Location
```

The generated FM4 registrar is unusually large. The build automatically
splits it into bounded helper functions so Clang can compile it reliably. Read
[Build notes](BUILD-NOTES.md) before changing function boundaries or the
generation pipeline.

Run a completed local build with:

```powershell
.\Launch FM4.bat
```

## Disc 2 content

Extract the four marketplace-content packages from your own Disc 2 into one
directory, then run:

```powershell
.\lfm4d2.bat "D:\path\to\disc2-packages"
```

The installer validates each STFS package's title ID and content type before
passing it to ReXGlue's content manager. Existing packages are skipped. After
installation, normal launches use the copy in user data and no longer require
the source directory.

## Post-launch DLC

`Launch FM4.bat` also installs FM4 marketplace DLC packages from your own
copy. It looks in the repository's `dlc-content` folder (git-ignored), or
the developer's local DLC folder hard-coded in the launcher if that is missing,
and passes it to `Run-Local.ps1 -DlcContentRoot`. The game accepts any STFS file
whose header identifies it as FM4 marketplace content, installs it once into
user data, and skips it on later launches. Other sources: the `FM4_DLC_ROOT`
environment variable and the `fm4_dlc_content_root` cvar. Set
`fm4_auto_install_dlc_content = false` to turn this off.

## Launchers

| Launcher | Purpose |
|---|---|
| `Build FM4.bat` | Runs the local builder (`tools\Build-Local.ps1`). |
| `Launch FM4.bat` | Runs the validated profile, installing DLC as above. |
| `Launch FM4 (Fast Readback).bat` | Same, with the experimental deferred readback sync (`-DeferredReadbackSync`). |
| `Launch FM4 (File Trace).bat` | Logs every file the game opens (`-TraceFiles`). |
| `Launch FM4 (Model Trace).bat` | Logs model archive reads to `serial_trace.log` (`-SerialTrace`). |
| `lfm4d2.bat` | One-time Disc 2 package install (see above). |

## Experimental work

These are under active development and are not part of the validated
profile:

- An opt-in native D3D12 render path (`render_target_path_d3d12 = "native"`,
  SDK patch `sdk-patches/0002-native-executor.patch`). In the tested race it
  matches the validated output and runs at about 48 FPS on average at 1x, and
  it supports 2x and 3x internal resolution. See
  [Native D3D12 renderer](docs/NATIVE_RENDERER.md).
- Deferred resolve readback sync (SDK patch 0001). See [Build notes](BUILD-NOTES.md).
- Tools that convert cars from other Forza titles (Forza Horizon 1 to 3,
  Forza Motorsport 2 and 3) into FM4 DLC packages, for use with your own
  copies of those games. See [Car port handoff](docs/CAR-PORT-HANDOFF.md) and
  the [global todo list](GLOBAL_TODO.md).

## Development notes

- [Release notes](docs/RELEASE_NOTES.md)
- [Current status and limitations](docs/STATUS.md)
- [Reusable findings and upstream candidates](docs/CONTRIBUTIONS.md)
- [Canonical documentation sources](docs/canonical-sources.md)
- [Native D3D12 renderer](docs/NATIVE_RENDERER.md)
- [Car port handoff](docs/CAR-PORT-HANDOFF.md)

## Repository policy

Do not commit extracted game files, generated source, user data, saves,
diagnostic captures, installed SDK files, or compiled binaries. Inspect every
staged change before publishing it.

Public artifacts follow the [builder-first release process](docs/RELEASING.md):
source and checksums only. The repository does not authorize publishing a
precompiled FM4 executable or generated translation.

This project is not affiliated with or endorsed by Microsoft, Xbox, Turn 10
Studios, or the Forza franchise.
