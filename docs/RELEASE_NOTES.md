# Release notes

## Windows x64 player download

The main alpha.2 download is `fm4-recomp-0.1.0-alpha.2-win-x64.zip`.
It contains the executable, both facade modules, required runtime DLLs,
app-local Visual C++ runtime, launcher and setup instructions. No local build is required.
Supply your own complete extracted Europe Game of the Year game files.
The ZIP contains no game files, DLC, Xbox system files or saves.
The source kit is an optional developer download.
See [player setup](https://github.com/Alexbeav/fm4-recomp/blob/main/docs/PLAYING.md).

This packages a fresh build of the alpha.2 title source against the retained
public SDK f16992c baseline. It adds no renderer or performance fix.
Build and dependency checks do not establish fresh gameplay acceptance.

## Tested behavior and limits

Gameplay evidence remains the Windows AMD64 validation from 2026-09-03.
Campaign progression is known good through Amateur and Clubman into Sportsman.
Later campaign progression is unvalidated.
The four Disc 2 packages can be installed from your own extracted media.
Online services and marketplace acquisition are outside the tested scope.

Fresh profiles can cache blank car thumbnails.
The conservative graphics configuration can stutter and run at about 30 FPS.
Higher resolution and timing overrides caused visual corruption in earlier tests.
See [current status](https://github.com/Alexbeav/fm4-recomp/blob/main/docs/STATUS.md) for the full tested boundary.

Source CI checks source packaging only. It does not qualify player binaries or gameplay.

## v0.1.0-alpha.1

This first experimental source release provides a builder-first path for
Forza Motorsport 4. It validates the user's three Xbox executable inputs,
builds the pinned ReXGlue SDK, generates the three recompiled modules locally,
and builds and launches FM4 in isolated local state.

Campaign progression is known good through Amateur and Clubman and into the
Sportsman tier. Later campaign progression has not been validated.

### Known issues

- Car-thumbnail generation is under investigation. Fresh profiles can produce
  a blank thumbnail where the showroom background appears without the car, and
  the blank result can remain cached.
- Performance and visuals need further improvement. The current
  correctness-oriented full-readback profile can stutter and run at about
  30 FPS despite low host-GPU utilization. Faster experimental configurations
  have caused visual corruption and are not release defaults.

Online services and downloadable marketplace acquisition are outside the
tested scope. Installation of the four Disc 2 content packages is supported
from the user's own extracted game media.

This release contains project source and build tooling only. It does not
contain game files, generated translation, saves, SDK binaries, or a compiled
FM4 executable. The project is licensed under `GPL-3.0-only`; users must supply
the validated inputs from their own legally obtained copy of the game.
