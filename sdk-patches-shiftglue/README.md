# FM4 SDK changes rebased onto shiftglue-sdk

These patches carry FM4's SDK changes over to shiftglue-sdk.

- Source: FM4's ReXGlue fork (Alexbeav/rexglue-sdk) at `f16992c`. It has 29
  commits on top of ReXGlue v0.9.0 (`cb58065`).
- Target: shiftglue-sdk at `0038a40`, the `thirdparty/shiftglue-sdk`
  submodule of pinyon-shift.

The series was made with `git format-patch` and has LF line endings. It
applies cleanly with `git am` on `0038a40`, and the result matches the
scratch branch.

**Not built or tested.** The patches were only cherry-picked and
conflict-resolved on Linux. They have not been compiled or run on Windows.

## Apply

```sh
git -C <shiftglue-sdk checkout> checkout -b fm4 0038a40
git -C <shiftglue-sdk checkout> am /path/to/sdk-patches-shiftglue/*.patch
```

## The series

Each patch keeps its original commit message and author. Where a conflict was
resolved by hand, the message ends with a `shiftglue port:` paragraph that
explains the resolution.

| # | Original | Subject | Result |
|---|----------|---------|--------|
| 0001 | 8f65be8 | follow parent control flow from reentry chunks | Conflict resolved. Kept shiftglue's `classifyTarget(target, fn)` call sites. |
| 0002 | 4b4487d | enter reentry chunks at their continuation label | Conflict resolved. The entry `goto` comes after shiftglue's resume-address switch. |
| 0003 | eb71287 | add cooperative guest checkpoint boundary | Conflict resolved. The include goes into `pch_h.inja`, and shiftglue's setjmp reentry is kept. |
| 0004 | 343ad0b | Generate stable address-based recompilation shards | Partial. Only the Windows clang UCRT defines are kept, because shiftglue's OutputPartition replaces the sharding. |
| 0005 | 6445c5b | Resolve parent-owned branch labels locally | Conflict resolved. Added as case 4 after shiftglue's owned-chunk case. |
| 0006 | 0bf0ed0 | Batch initial function registration | Conflict resolved. Adds the `notify` parameter, which came from the skipped cebbe53. |
| 0007 | 74c5c03 | Add lightweight guest FPS overlay statistics | Conflict resolved. The frame is recorded in `RefreshGuestOutput`. |
| 0008 | 342679f | Mount writable guest cache devices | Conflict resolved. Adds cache0: and cache1:. shiftglue already mounts cache: from cache_root. |
| 0009 | ed0256a | Add a guest vblank frequency override | Conflict resolved. The override replaces shiftglue's FH1 vblank cadence. |
| 0010 | 549dc8e | Use a high-resolution timer for sub-quantum sleeps | Applied cleanly. |
| 0011 | f16992c | fix(build): support current Windows toolchains | Conflict resolved. The clock_cast guard is merged and excludes libstdc++. The libmspack path change applies as is. |

## Already in shiftglue

These commits were not exported because shiftglue already contains them, or a
newer equivalent:

| Original | Subject | shiftglue equivalent |
|----------|---------|----------------------|
| bab1acb | resume guest return chains after context switches | `XThread::Reenter`, using setjmp/longjmp instead of an exception (079c10e). |
| 1710bef | Register interior-PC resume aliases for table dispatch | `resumableReturnAddresses`, `ctx.dispatch_address` and the extended `resume_entry_test` (079c10e). |
| 97a7fc8 | Treat empty resolve rectangles as successful no-ops | The no-op is in `draw_util::GetResolveInfo`. The FM4 commit's one-shot failure-state logger is not ported. |
| 2104326 | Unregister recompiled modules and reset heaps on XEX unload | Identical. The patch reverse-applies cleanly. |
| 0cb82d3 | Handle XAM message 0x2B003 | Identical apart from formatting. |
| 8387475 | Match Xenon vmsum reduction and overflow semantics | Identical. The patch reverse-applies cleanly. |
| 60bdc6e | Make the offline profile gamertag configurable | `user_name` cvar with gamertag sanitizing (7ca8128). |

## Skipped because of conflicts

Each of these needs a manual port.

- **cebbe53 Add parent-backed continuations and runtime evidence.** This is the
  blocker for FM4 codegen. It conflicts in 10 files:
  - `function_node.h`: shiftglue's `resumableReturnAddresses` against FM4's
    `parentBackedAliases`.
  - `ppc/context.h`: shiftglue moved `PPCFunc` and the macros to
    `rex/ppc/func.h`. It also adds the crash-diagnostic fields,
    `PPCFunctionScope` and the indirect trace ring.
  - `init_h.inja`: shiftglue moved the SDK block to `pch_h.inja`. The
    memory-provenance `REX_STORE_*` and `REX_MM_STORE_*` macros must move
    there.
  - `function_graph.cpp`: two hunks in `emitCpp` and the entry dispatch.
  - `project_recompiler.cpp`: shiftglue's incremental stamp and skip logic.
  - `function_dispatcher.cpp`: the invalid-function trap message.
  - `input_system.cpp`, `mnk_input_driver.{h,cpp}`: 7 hunks against
    shiftglue's input rework.
  - `tests/unit/CMakeLists.txt`.

  FM4's `fm4_functions.toml` has about 283k `parent =` entries. Without this
  commit, shiftglue treats each of them as an upstream chunk with its own
  emitted body, not as an alias of the parent's body.
- **8a86376, 72d521a, 1332af7.** These continue the continuation-alias work and
  need `isParentBackedAlias` and `parentBackedAliases` from cebbe53.
- **3f88fbb, 9026fce (GPR guard).** They need `ctx.current_function` and
  `current_instruction` from cebbe53. They also conflict in `_indirect_call.inja`
  with shiftglue's `ctx.dispatch_address`, and in `init_h.inja`,
  `builders/context.cpp`, `thread_state.cpp`, `src/system/CMakeLists.txt` and
  the tests list.
- **8e7e36c Report guest exceptions across fiber boundaries.**
  `guest_exception_report.cpp` reads `current_function` from cebbe53.
  `fiber.h` and `fiber_posix.cpp` conflict with shiftglue's assembly POSIX
  fiber switch: `rex_fiber_start` calls the entry directly, so it has no
  trampoline to catch in. The Win32 part, `fiber_win32.cpp`, merged cleanly.
- **e3bbb5f Handle completed guest fibers without termination.** Builds on
  8e7e36c's trampoline and `return_fiber_`. For FM4 on Windows, porting the
  Win32 halves of 8e7e36c and e3bbb5f is the minimum.

## Graphics commits not ported

shiftglue's D3D12 path is FH1-only. These commits change the Xenos renderer it
removed, so none of them are ported:

- **b2a2054.** Adds the `readback_resolve=auto` mode and the
  `readback_resolve_max_kb` cvar.
- **74185b3.** Adds the direct guest-RAM readback, including the
  `d3d12_readback_resolve_host_copy` cvar.
- **2746217.** Fixes signed and integer resolves, adds RT tracing, and includes
  about 135k lines of regenerated resolve shader bytecode.

2746217 has fixes that matter for correctness:

- the integer `num_format` texture scale in `dxbc_translator_fetch.cpp`, with
  per-format component widths in `info_formats.cpp`;
- `resolve_check_number_format`;
- `gamma_decode_pwl_resolve`.

None of these is in shiftglue.

The uncommitted deferred-readback-sync patch in the FM4 fork
(`sdk-patches/0001`) targets the same removed path.
