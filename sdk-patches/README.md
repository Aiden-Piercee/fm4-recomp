# SDK patches

`tools/Build-Local.ps1` applies every `*.patch` in this folder, in name order,
on top of the pinned SDK commit from `release/release-policy.json`. Any other
modification of the SDK checkout is still refused, and the build receipt
records the hash of each applied patch.

Every patch keeps existing behavior by default: new code paths sit behind
runtime settings that are off unless enabled. The validated profile in
[`docs/STATUS.md`](../docs/STATUS.md) stays the correctness reference.

To rebuild only the patched GPU plugin into an existing local build:

```powershell
pwsh -File .\tools\Build-Local.ps1 -GameDataRoot "D:\Games\FM4-extracted" -SdkOnly
```

## Series

- `0001-d3d12-deferred-readback-sync.patch`: `d3d12_readback_resolve_deferred_sync` waits for resolve readback once per guest-visible sync point instead of after every resolve.
- `0002-native-executor.patch`: opt-in native render-target path (`render_target_path_d3d12 = "native"`), ported from shiftglue-sdk (BSD-3-Clause, see `THIRD_PARTY_NOTICES.md`). Also adds `native_executor_stencil_ref_transfers` and the `frame_time_stats` log lines.
- `0003-d3d12-readback-early-submit.patch`: `d3d12_readback_resolve_early_submit` submits the readback copy right away and waits only for that submission.
- `0004-frame-hitch-log.patch`: `frame_hitch_log_ms` logs a breakdown of every frame slower than the threshold.
- `0005-hitch-cpu-breakdown.patch`: hitch lines also break command-processor CPU time down by work type.
- `0006-hitch-texture-breakdown.patch`: hitch lines also break texture time into create/upload/load and count evictions.
- `0007-d3d12-placed-texture-heaps.patch`: `d3d12_texture_placed_heaps` places textures in pre-made heaps instead of creating a committed resource for each one.
- `0008-precise-vblank-timer.patch`: `vblank_precise_timer` delivers guest vblanks on time instead of up to 1-2 ms late.
- `0009-native-executor-single-sample-msaa.patch`: `native_executor_single_sample_msaa` creates guest MSAA surfaces with one host sample (no anti-aliasing) to save GPU bandwidth.
- `0010-native-executor-max-msaa-samples.patch`: `native_executor_max_msaa_samples` caps host MSAA samples; `2` stores guest 4x surfaces as 2x, keeping some anti-aliasing at lower cost.
