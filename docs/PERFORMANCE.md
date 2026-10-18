# Performance options

Everything on this page is opt-in. `fm4/fm4_runtime.toml` stays the validated
correctness profile described in [Current status](STATUS.md), and every setting
below is off unless a launch enables it. Results here are performance
measurements, not a new gameplay validation.

Try a setting for one launch with `tools/Run-Local.ps1 -RuntimeArgs` (see
[Build notes](../BUILD-NOTES.md#per-run-runtime-settings)). The SDK changes
behind these settings are in [`sdk-patches/`](../sdk-patches/README.md).

## Deferred resolve readback sync

`d3d12_readback_resolve_deferred_sync` (SDK patch 0001). With
`readback_resolve = "full"` and `d3d12_readback_resolve_host_copy`, the command
processor used to wait for the GPU after every resolve. With this setting it
records each copy into guest RAM and waits once, at the next point where the
guest can observe command-stream progress: fences, interrupts, memory and
scratch-register writebacks, swaps and command-processor stalls. The guest
still never sees a signal before the resolved data is in guest RAM.

Enable it per run with `Run-Local.ps1 -DeferredReadbackSync`.

## Native render-target path

`render_target_path_d3d12 = "native"` (SDK patch 0002). The default `rov` path
emulates Xenos EDRAM in a rasterizer-ordered view. The native path renders
into host render targets instead, and runs EDRAM ownership transfers between
aliased surfaces, clears and resolves as GPU shaders. Resolved data still goes
through the unchanged `readback_resolve` code. Shaders still come from the
runtime Xenos-to-DXBC translator.

- `native_executor_stencil_ref_transfers`: writes a depth surface's ownership
  transfer (depth and all stencil bits) in one pass with a shader stencil
  reference, instead of a depth pass plus up to eight per-bit stencil passes.
  EDRAM transfers are the largest GPU cost at higher internal resolutions, and
  this roughly halves them at 2x.
- `frame_time_stats` (default on) and `frame_time_stats_window` (default 300
  swaps) log a `frame times/<n>f` line with the average, percentiles, frames
  over 17.5/34/50 ms, the vblanks-per-swap histogram, and the per-frame command
  processor idle, `WAIT_REG_MEM` and GPU-wait time.

The executor is ported from shiftglue-sdk; see
[`THIRD_PARTY_NOTICES.md`](../THIRD_PARTY_NOTICES.md).
