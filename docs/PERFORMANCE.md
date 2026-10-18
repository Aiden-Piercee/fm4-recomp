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
