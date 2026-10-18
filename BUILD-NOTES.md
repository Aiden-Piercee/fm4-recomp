# FM4 build notes

## Pinned Windows toolchain

Build the SDK and FM4 with the same C++ ABI. The validated packaging build uses
LLVM 22's MSVC target (`C:\Program Files\LLVM\bin\clang++.exe`) for both. The
Retcomm compiler targets MinGW/libc++; it must not consume an SDK built for the
MSVC ABI because exported C++ symbols have different names and standard-library
types.

The pinned SDK needs a feature-gated compatibility definition of
`std::chrono::clock_time_conversion` and `std::chrono::clock_cast` when
`__cpp_lib_chrono` is older than C++20. Its libmspack build must also use the
canonical `libmspack/libmspack/mspack` source directory. The cabextract path is
a symbolic-link mirror that may be checked out as one-line text files on
Windows. These fixes are recorded in SDK commit
[`f16992c`](https://github.com/Alexbeav/rexglue-sdk/commit/f16992c92e610c2d71773994436b442d9946f4e1).

The builder-first release path installs the SDK under `.local\sdk-install` and
builds FM4 under:

```text
.local\work\fm4\out\build\win-amd64-release
```

It asks the SDK CLI to generate its standard CMake bootstrap locally, copies
only allowlisted project source into `.local\work`, and generates all
game-derived translation there.
The 2026-09-03 end-to-end test completed the 293-target FM4 build and a bounded
15-second startup smoke test. The original known-good executable retained SHA-256
`4339E81F9CE190099F8756616CEC2C8AA6BB105E33B38E577A103196D2311281`.

## Generated function registrar

FM4 generates approximately 292,000 function registrations in one C++ source
file. Clang 22 exhausts memory when it optimizes the original single function.
Compiling the original function at `-O0` can leave late registrations unusable
at runtime.

The CMake build runs the splitter automatically when code generation changes
the registrar. To apply it manually for inspection, run:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ".\Split FM4 Registrar.ps1"
```

The idempotent script rewrites `fm4/generated/default/fm4_register.cpp` into
bounded, non-inlined helper functions. `fm4/CMakeLists.txt` compiles this
generated file and the startup-only `fm4_init.cpp` dispatcher at `-O0`. The
dispatcher otherwise exhausts memory during an optimized compile on a busy
development host. Recompiled game-code translation units keep the release
optimization settings.

SDK configurations that precompile the generated `fm4_pch.h` must exclude the
two `-O0` startup sources. Clang rejects a PCH built at the release optimization
level when the consuming translation unit uses `-O0`.

Before testing a rebuilt executable, verify that the script reports the same
registration count before and after splitting.

The main executable dispatch table is populated from `PPCFuncMappings` in
`fm4/generated/default/fm4_init.cpp`. A targeted manual rebuild after adding a
function must compile that file, `fm4_register.cpp`, and every affected
`fm4_recomp.*.cpp` segment before relinking. `fm4_register.cpp` alone does not
update the main-image dispatch table.

When the manifest's dynamic-module set changes, also rebuild
`generated/default/module_registry.cpp`. Relinking with a stale registry can
silently omit a facade DLL and fail only when the game first loads that module.

## SDK patches

The builder applies `sdk-patches/*.patch` in name order on top of the pinned
SDK commit and refuses any other SDK modification. A rebuild reverses the
previously applied series first, so editing or adding a patch takes effect on
the next build. `-SdkOnly` rebuilds only the SDK and refreshes
`rexgpu-xenos.dll` in an existing local build. See
[`sdk-patches/README.md`](sdk-patches/README.md).

## Per-run runtime settings

`tools/Run-Local.ps1 -RuntimeArgs "--name=value,--other=value"` changes runtime
settings for one launch without editing `fm4/fm4_runtime.toml`. The overrides
are written to the top of the deployed `fm4.toml`, because the runtime applies
the config file after the command line. `--name` sets `true`, `--no-name`
sets `false`.

## Native executor shaders

`sdk-patches/0002` adds the native executor's HLSL shaders but no compiled
bytecode. After applying the patches, `tools/Build-NativeShaders.ps1` compiles
them into `src/graphics/shaders/bytecode/d3d12_5_1/native_*.h` in the SDK
checkout with `fxc.exe` from the Windows SDK, and skips shaders whose headers
are newer than the sources. Set `FM4_FXC` to use a specific `fxc.exe`.
