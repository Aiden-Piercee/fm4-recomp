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

## Download and start here

Download **`fm4-recomp-0.1.0-alpha.2-win-x64.zip`** from the [Latest release](https://github.com/Alexbeav/fm4-recomp/releases/latest).
This is the ready-to-run Windows x64 build. Extract it, copy your own complete extracted
Europe Game of the Year Disc 1 into its `game` folder, then double-click `Launch FM4.bat`.
The ZIP includes the executable and required DLLs. It contains no game files or saves.
No compiler, SDK installation or PowerShell is needed to play.
See [player setup and limitations](docs/PLAYING.md).

The source kit and automatic GitHub source archives are for rebuilding the project.
For a local source build, use Windows x64 and PowerShell 7, Git, CMake 3.25 or newer,
Ninja, LLVM 22 targeting MSVC, and Visual Studio C++ build tools with a Windows SDK.
Use a Developer PowerShell session and follow [Local build](#local-build) and
[Build notes](BUILD-NOTES.md).

Your extracted game folder must contain `default.xex`, `XMediaFacade_default.xex`,
and `SpeechFacade_default.xex` directly inside it, plus the full `Media` tree.
The validated game is Europe Game of the Year, title ID `4D530910`, media ID `42C67824`.
The source builder verifies the [input identities](release/release-policy.json).
An ISO alone is not an extracted game folder.

## Project status

This is an experimental alpha release. On the tested Windows AMD64
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

- Windows AMD64 and PowerShell 7
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
.\Launch FM4 Disc 2 Install.bat "D:\path\to\disc2-packages"
```

The installer validates each STFS package's title ID and content type before
passing it to ReXGlue's content manager. Existing packages are skipped. After
installation, normal launches use the copy in user data and no longer require
the source directory.

## Development notes

GitHub checks source packaging on pushes and pull requests.
These checks do not compile or run the game.
Maintainers prepare draft releases with the
[release workflow](docs/RELEASING.md#github-automation).


- [Release notes](docs/RELEASE_NOTES.md)
- [Current status and limitations](docs/STATUS.md)
- [Reusable findings and upstream candidates](docs/CONTRIBUTIONS.md)
- [Canonical documentation sources](docs/canonical-sources.md)

## Repository policy

Do not commit extracted game files, generated source, user data, saves,
diagnostic captures, installed SDK files, or compiled binaries. Inspect every
staged change before publishing it.

Public artifacts follow the [release process](docs/RELEASING.md). The main download
is the win-x64 player ZIP; source archives remain available for rebuilding.
Game files, raw generated translation, saves and caches are excluded from public packages.

This project is not affiliated with or endorsed by Microsoft, Xbox, Turn 10
Studios, or the Forza franchise.
