# Native D3D12 renderer: findings and plan

Status (2026-10-11): the interception plan below is still at step 1 (research).
Separately, an opt-in SDK-level native render path exists and matches the ROV
baseline in the tested race at 1x and 2x (see "Native executor path"). It is
not the default; `rov` stays the validated profile.

## Why

The current build emulates the Xenos GPU at the PM4/register level. FM4's
validated profile needs the ROV EDRAM path and full resolve readback, which
leaves races at roughly 20-27 FPS with about 30 ms of GPU work per frame on an
RX 7900 XTX. The RTV path renders a black screen for FM4 (tested
2026-10-09, with and without `direct_host_resolve`).

A native renderer intercepts FM4's statically linked Xbox 360 Direct3D library
in the recompiled code and draws with D3D12 directly: render targets become
host textures, resolves become GPU copies, and EDRAM emulation disappears.

## Interception mechanism

Every generated function is declared as

```cpp
__attribute__((alias("__imp__sub_X"))) __attribute__((weak, noinline)) void sub_X(PPCContext&, uint8_t*);
```

A strong `extern "C" REX_FUNC(sub_X)` in project code replaces it for direct
calls and for the dispatch table, while `__imp__sub_X(ctx, base)` still runs
the original translation. `fm4/src/d3d_probe.cpp` uses this to observe calls.
`[[midasm_hook]]` with `return_on_true` is the alternative for conditional
replacement inside a function.

## D3D library map (static analysis, 2026-10-09)

Addresses are guest addresses of `sub_X` functions. Confidence: H/M/L.

### Device and submission
| Address | Role | Conf. |
|---|---|---|
| 0x826EE598 | Direct3D_CreateDevice (0x6080-byte device) | H |
| 0x826FA7F8 | Device init; stores device global | H |
| 0x826FA1D8 | VdInitializeEngines + interrupt callback registration | H |
| 0x826EA920 | Ring (32 KB) and command buffer (2 MB) init, ME_INIT | H |
| 0x826EA1C8 | Graphics interrupt callback (CP source 1, vblank source 0) | H |
| 0x822DA868 | KickOff: INDIRECT_BUFFER(_PFD) into ring, writes CP_RB_WPTR | H |
| 0x826EA340 | Submit-segment wrapper | M |
| 0x822FE840 | Segment flush | H |
| 0x822FE9C8 | MakeSpace (142 callers) | H |
| 0x82390ED0 | Reserve N dwords | H |
| 0x822DA7B8 | Wait for ring space | H |
| 0x8237F598 | D3DDevice_Swap (calls VdSwap) | H |

Device global: indirect through 0x82000B44 / 0x82000B48 (chosen by process
type). Device fields: +48 write cursor, +52 segment end, +56 soft limit,
+15216 front buffer, +15232 swap parameters, +16952 frame counter,
+15332 critical section, +21924 optional interface that bypasses the ring.

### Command emission
| Address | Role | Conf. |
|---|---|---|
| 0x8233A718, 0x8234CD90 | Auto-index draws (DRAW_INDX, 2 dwords) | H |
| 0x82310B30, 0x8234D600 | Indexed draws (DRAW_INDX, 4 dwords) | H |
| 0x82310928 | Dirty shader-constant flush (bases 0x4000 VS, 0x4400 PS) | H |
| 0x82310FA8 | Dirty render-state register flush (0x2000-0x2380, 0x4900) | H |
| 0x8230F4F0 | Dirty fetch-constant flush (0x4800) | H |
| 0x82343170 | Shader commit: SQ_PROGRAM_CNTL, IM_LOAD VS and PS | H |
| 0x823430C0 | LOAD_ALU_CONSTANT | H |
| 0x823614E8 | Render target / HiZ state | H |
| 0x8230FFE0 | Window offset and scissor per tile (predicated tiling) | M |
| 0x823488E0 | Resolve: RB_COPY_*, built-in shader, DRAW_INDX_2 | H |
| 0x822B9AC0 | INDIRECT_BUFFER emitter (19 call sites; possible pre-recorded buffers) | H |
| 0x822A7F40 | Occlusion query (EVENT_WRITE_ZPD) | H |
| 0x822A8108, 0x822C1DD8 | Fences / CPU-GPU sync | M |
| 0x826E....-0x826F.... | D3D internal blits/clears with embedded shaders | M |

Packet headers in hot paths carry the predicate bit (`0xC0012201`), and render
state is written as runtime-sized type-0 register blocks, not SET_CONSTANT.

## Plan

1. **Map (in progress).** Confirm roles at runtime with `fm4_d3d_probe`:
   per-frame call counts and sample arguments.
2. **Shadow state.** Hook the flush helpers, shader commit, draws, resolve and
   swap; build a host-side model of render targets, textures, shaders and
   constants from the device's register shadow. Still render through the
   emulator; validate the model against it.
3. **Native draws.** Translate Xenos microcode with the SDK's existing shader
   translator, create host render targets keyed by EDRAM base/format, issue
   D3D12 draws, and stop emitting the guest PM4 for handled draws.
4. **Native resolves.** GPU-to-GPU copies into textures; CPU readback only for
   the resolves the game actually reads on the CPU.
5. **Per-scene fixes, then PC features** (internal resolution, widescreen).

## Open questions

- Values at 0x82000B44/48; the +21924 interface; whether pre-recorded command
  buffers (INDIRECT_BUFFER from 0x822B9AC0) carry draws that bypass the draw
  functions.
- Which pre-swap calls (0x822B9AC0, 0x822B2E70, 0x82355E80) are resolves or
  render-target changes.
- How FM4 consumes resolved data on the CPU (the reason full readback is
  required today).

## Native executor path (SDK patch 0002, 2026-10-09)

`sdk-patches/0002-native-executor.patch` ports shiftglue-sdk's (Pinyon Shift,
BSD-3-Clause) native D3D12 executor into the SDK as a third render target path.
It is opt-in; `rov` stays the default and is unchanged.

- Enable per run: `render_target_path_d3d12 = "native"` (for example
  `--render_target_path_d3d12=native`). The render target cache then only
  configures host render target (RTV/DSV) pipelines; the executor owns render
  target binding, EDRAM tile ownership and transfers between aliased surfaces,
  clears and resolves. Shaders still come from the runtime Xenos-to-DXBC
  translator; no shader packs.
- Resolves are compute dispatches into the shared-memory mirror in the guest
  texture layout. The written range then goes through the existing
  `readback_resolve` code unchanged (`full` + `d3d12_readback_resolve_host_copy`
  and `d3d12_readback_resolve_deferred_sync` behave as on the ROV path).
- Resolve destination formats: 8_8_8_8, 2_10_10_10, 32_FLOAT,
  16_16_16_16_FLOAT and depth use shiftglue's prebuilt shaders. 8_8, 16_16 and
  16_16_16_16 (honoring the copy destination number format) use
  `native_resolve_memory_ext.cs.hlsl`, compiled at first use with
  D3DCompiler_47.dll. Anything else is skipped and counted.
- Every 300 frames the log prints `native executor frame=... skips={...}
  stats={...}` plus skipped resolves by kind and surface memory.
- Internal resolution: `--resolution_scale=2` (or 3; or
  `--draw_resolution_scale_x/_y`) works on the native path. Surfaces, transfers
  and clears run at the scale, resolves write the texture cache's scaled resolve
  range (made current exactly as the render target cache does), and the
  command processor's existing downscale step still delivers 1x data to guest
  RAM for `readback_resolve = "full"`. Non-square or >3x requests log a warning
  and fall back to 1x.
- Removed from the port: frame census/dumps/replay, shader packs, reflection
  cube import, GPU profiling, FH1 vblank pacing, the depth-overwrite shortcut
  (those draws take ordinary transfers).

First test (automation results `0110-run-native`, baseline `0111-run-baseline`,
same inputs, deferred sync on): the title screen and main menu render; the menu
screenshots at 45 s and 88 s match the ROV baseline almost exactly (mean
absolute difference 0.009 per channel). No executor skips. Both paths reach 300
frames per 5 s in the menu. Read-back waits drop from about 2.7 s to 1.85 s per
300 frames. Races and the k_16_16 resolves noted in CONTRIBUTIONS.md are not
tested yet.

Race test (template `.local/automation/templates/race-freeplay.json`: Free
Play > Quick Race > Abarth 500 > Bernese Alps Festival Circuit). Before the
extended resolve shader (`0204-race-native`) the native path skipped
k_8_8_8_8 -> k_8_8 and k_16_16 -> k_16_16 resolves, and the road surface and
trackside banners were wrong. With it (`0206-race-native`) nothing is skipped
and the race matches the ROV baseline (`0203-race-default`) visually,
including the car shadow on the pre-race screen. Race window (about 155-258 s,
from `readback stats/300f` timestamps): native averages 47.6 FPS (27-60) with
9.6 ms of read-back wait per frame; ROV averages 26.7 FPS (17-30) with 22.7 ms
(`0207-race-default`, rerun on the final build: 26.3 FPS).

## Frame pacing and resolution scale (2026-10-09, native path)

Per-frame columns from the `frame times/300f` lines (cp idle / wait_reg_mem /
gpu wait, ms per frame):

| Scene (run) | FPS | vblanks/swap | idle / wrm / gpu |
|---|---|---|---|
| Main menu, 1x (0317) | 60.0 | 1:300 | 0 / 9 / 6 |
| Autovista, 1x (0317) | 30.0 | 2:300 every window | 4 / 15 / 9 |
| Pre-race menu, 1x (0317) | 30.0 | 2:300 | 0 / 16 / 12 |
| Race, 1x (0322) | 60.0 | 1:297-300 | 0 / 3 / 8 |
| Autovista, 3x (0318) | 30.0 | 2:300 | 0 / 3 / 26 |
| Race, 2x (0315, 0323) | 35-36 | mixed 1/2 | 0 / 4-9 / 13-17 |
| Race, 3x (0318) | 27 | 2 and 3+ | 0 / 1-8 / 25-29 |
| Autovista, 1x, vblank 120 Hz (0319) | 60.0 | 2:300 | 0 / 2-3 / 9 |
| Race, 1x, vblank 120 Hz (0319) | 56 | mostly 2 | 0 / 2-4 / 8-12 |

Autovista and the pre-race screen are paced by the guest at two vblanks per
swap: at 1x the GPU wait is only 9-12 ms and the CP spends the rest of the
frame in WAIT_REG_MEM waiting for the guest. With
`vblank_frequency_override=120` the same scenes keep the 2:300 histogram but
run at 60 FPS, and the race clock still advanced 12.2 s per 12 s of wall time
(screenshots 0372/0384/0396 of run 0319), so game time did not speed up;
menus then run at 120 FPS. Races are not capped: at 1x they hold one vblank per
swap; at 2x and 3x the GPU wait (resolves, transfers and the full read-back
waits) pushes frames past 16.7 ms.

Scale results: 2x (0315) renders correctly (car shadow, road, banners, no
"stained-glass" corruption, no skips) and is much sharper. 3x (0318) also
renders but is GPU bound (26 ms GPU wait per frame), and menus drop to 30 FPS.

## GPU cost breakdown and transfer experiments (2026-10-10, native path)

SDK patch 0002 also adds these cvars (all default off or unchanged):

- `native_executor_gpu_profile`: logs `native executor gpu ms/frame over 300
  frames` with GPU timestamp totals for all submissions and for the executor's
  transfers, resolves, clears and resolve read-back copies.
- `native_executor_stencil_ref_transfers`: writes a depth destination's
  ownership transfer (depth and all stencil bits) in one pass with a
  shader-specified stencil reference, instead of a depth pass plus up to eight
  per-bit stencil passes.
- `frame_time_stats` / `frame_time_stats_window` (default 300 swaps): the
  `frame times/<n>f` log lines.
- `deferred_readback_flush_mask` (default -1 = all, the validated setting):
  experimental bit mask of the guest sync points where deferred readback is
  awaited. Clearing bits can leave the guest reading stale resolve data.

GPU ms per frame (median and maximum over the 300-frame windows of each run;
all runs native path, deferred readback sync on):

| Run | Setup | Submissions | Transfers | Resolves | Other/draws |
|---|---|---|---|---|---|
| 0402, 0404 | 1x | 7.3-8.6 (max 10.7) | 3.7-4.3 | 0.2-0.3 | 2.6-4.2 |
| 0403 | 2x | 17.1 (max 17.5) | 12.3 | 0.5 | 4.0 |
| 0420 | 2x | 14.5 (max 20.2) | 9.1 (max 13.3) | 0.7 | 4.3 |
| 0421 | 2x, stencil-ref transfers | 8.6 (max 14.9) | 4.4 (max 5.7) | 0.5 | 3.4 |
| 0422 | 2x, `readback_resolve=none` | 12.1 (max 18.2) | 8.0 | 0.3 | 3.6 |

- EDRAM ownership transfers are the largest GPU cost, and they grow most with
  resolution scale. Single-pass stencil-reference transfers roughly halve them
  at 2x. Visual equivalence of that mode has not been written up yet.
- With full readback, the 2x race runs at about 30 FPS (0420, 0421; up to 37 FPS in places): most frames
  take two vblanks, with 15-21 ms of GPU wait and 5-10 ms of WAIT_REG_MEM per
  frame, and about 5 s of read-back waiting per 300 frames.
- With `readback_resolve=none` (0422) the run held about 60 FPS with 0 ms of GPU
  wait almost throughout, including stretches that run at 30 FPS in the other
  runs, and the game exited at 216 s of a planned 322 s. So the run probably did
  not follow the same scene sequence, and its visuals have not been reviewed.
  Automatic readback, which skips some resolves, caused flicker and texture
  corruption in earlier ROV tests (docs/STATUS.md). Do
  not read 0422 as a usable setting or a like-for-like speed-up. The next step
  is still to find which resolves FM4 reads on the CPU (open question 3 above).
