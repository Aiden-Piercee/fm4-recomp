# Third-party software notices

The FM4 source release does not contain third-party binaries or generated game
translation. It references and locally builds the following direct
dependency:

- [ReXGlue SDK](https://github.com/Alexbeav/rexglue-sdk), pinned by
  `release/release-policy.json`, is licensed under the BSD 3-Clause License.
  It includes or links additional components under their own terms and
  contains code derived from [Xenia](https://github.com/xenia-project/xenia),
  also under the BSD 3-Clause License.

The pinned SDK checkout is the authority for the complete dependency list and
the license texts applicable to a local build. Anyone who redistributes SDK
files or locally produced binaries must preserve the notices and satisfy the
licenses shipped with that exact SDK source. The FM4 source packager does not
package those files.

## shiftglue-sdk native executor (SDK patch)

`sdk-patches/0002-native-executor.patch` adds an opt-in D3D12 render path
(`render_target_path_d3d12 = "native"`) to the pinned ReXGlue SDK. Its native
executor (`src/graphics/d3d12/native_executor.cpp`, the `native_edram_*`
headers and sources, and the `native_*` HLSL shaders and their DXBC bytecode
headers) is ported from the native executor of
shiftglue-sdk as vendored by the Pinyon Shift project
(Forza Horizon 1), commit `0038a40`:

- Copyright (c) 2026, Tom Clay <tomc@tctechstuff.com>
- Portions derived from the Xenia project, Copyright (c) 2022, Ben Vanik and
  Xenia project contributors
- Licensed under the BSD 3-Clause License. The full license text is carried in
  the patch as `src/graphics/d3d12/NATIVE_EXECUTOR_LICENSE.txt`, and each ported
  file keeps the attribution header.

## Project artwork

The repository includes the user-supplied project image
`assets/fm4-recompiled-hero.png`. Names, logos, vehicle designs, and other
third-party visual elements in that image are not offered under the project's
GPL license. All such rights remain with their respective owners. The project
is not affiliated with or endorsed by Microsoft, Xbox, Turn 10 Studios,
Ferrari, or the Forza franchise.
