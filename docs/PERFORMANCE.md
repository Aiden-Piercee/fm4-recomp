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

## Early submit for deferred readback

`d3d12_readback_resolve_early_submit` (SDK patch 0003, needs deferred sync).
The deferred sync point used to wait for every queued GPU operation, including
draws recorded after the resolve. With this setting the command list is closed
right after the copy into guest RAM, and the sync point waits only for the
submission holding the latest copy, which has usually finished by then.

## Finding hitches

Set `frame_hitch_log_ms` (SDK patch 0004, default 0 = off) to log every frame
slower than that many milliseconds, for example `--frame_hitch_log_ms=40`:

```text
frame hitch #4858: 68.7 ms, 8 vblanks | cp idle 0.0 ms (waiting for the game), wait_reg_mem 0.0 ms, gpu wait 6.6 ms, cp busy 62.1 ms
```

`cp idle` is time spent waiting for the game to submit work, `wait_reg_mem` is
guest-requested stalls, `gpu wait` is the command processor waiting for the
host GPU, and `cp busy` is the rest: command processing on the CPU. A
separate log file per run (`--log_file=...`) keeps runs comparable.

With SDK patch 0005, each hitch line also lists where the command
processor's CPU time went (categories of at least 0.5 ms) and how many draws,
resolves and submissions the frame had:

```text
... cp busy 62.1 ms | cpu ms: primitives=1.6 textures=47.1 vertex_buffers=0.9 resolves=6.7 submit=1.4 | draws=970 resolves=41 submissions=40
```

The categories are `primitives`, `targets`, `pipelines`, `textures`,
`bindings`, `vertex_buffers`, `resolves`, `swap` and `submit`. Timing is only
taken while `frame_hitch_log_ms` is set.

SDK patch 0006 splits the texture time further:

```text
| tex: create=3.7ms/197 upload=7.4ms/40.4MB scaled_commit=0.0ms load=0.2ms/222 (30 scaled) evicted=0 cache=505MB
```

`create` is host texture creation (time/count), `upload` copying guest memory
to the GPU, `scaled_commit` committing scaled-resolve memory, `load` the
untiling dispatches, `evicted` textures dropped by the cache budget, and
`cache` the cache's current size.

## Texture cache budget

The texture cache's defaults (`texture_cache_memory_limit_soft = 384`,
`texture_cache_memory_limit_hard = 768`,
`texture_cache_memory_limit_soft_lifetime = 30`) evict textures that were not
used for 30 seconds once the cache is over the soft limit. At 2x, FM4's race
textures pass that limit early, and a lap takes longer than 30 seconds, so
track textures were destroyed and recreated on every lap. On a GPU with plenty
of memory, a larger budget removes those evictions, for example
`--texture_cache_memory_limit_soft=4096,--texture_cache_memory_limit_hard=8192,--texture_cache_memory_limit_soft_lifetime=600`.

## Placed texture heaps

`d3d12_texture_placed_heaps` (SDK patch 0007, default off). FM4 streams track
textures in while driving, and each new texture used to be its own
`CreateCommittedResource`, which costs about 0.5 ms on the tested AMD driver.
A frame that streams 150-200 textures then stalls for 80 ms or more. With this
setting textures are placed in large heaps created up front, and freed ranges
are reused once the GPU is done with them. Creating 197 textures in one frame
dropped from about 85 ms to 3.7 ms.

- `d3d12_texture_heap_size_mb` (default 64): size of each heap. Textures larger
  than half a heap still get their own committed resource.
- `d3d12_texture_heap_prealloc_mb` (default 0): heap memory to create at
  startup, so heap creation does not happen during a race.

## Precise guest vblank timer

`vblank_precise_timer` (SDK patch 0008, default off). The guest vblank thread
woke from a 1 ms sleep, which on Windows often overshoots by 1-2 ms, so vblanks
arrived late by a varying amount and the game's frame spacing inherited that
jitter. With this setting the thread sleeps only while more than about 2 ms
remain, then yields until the vblank is due. At a locked 60 FPS this halved the
frames that arrived late (over 17.5 ms), at the cost of a little CPU time on
the vblank thread.

## Experimental 1440p profile

`Launch FM4 (Performance 1440p).bat` combines the settings above for 2x
internal resolution (2560x1440):

```toml
render_target_path_d3d12 = "native"
resolution_scale = 2
d3d12_readback_resolve_deferred_sync = true
d3d12_readback_resolve_early_submit = true
native_executor_stencil_ref_transfers = true
texture_cache_memory_limit_soft = 4096
texture_cache_memory_limit_hard = 8192
texture_cache_memory_limit_soft_lifetime = 600
d3d12_texture_placed_heaps = true
d3d12_texture_heap_prealloc_mb = 1024
readback_resolve = "fast"
vblank_precise_timer = true
```

- `readback_resolve = "fast"` is the experimental part. Full readback made the
  command processor wait about 7 ms per frame for the GPU at 2x, so about a
  quarter of race frames missed their 60 FPS slot. `fast` still copies every
  resolve, but hands the game the previous frame's data instead of waiting.
  The tested races showed no visual errors; one brief glitch may have appeared
  on the pre-race screen. `auto` readback, which skips resolves, remains known
  to cause flicker and texture corruption and is not used.
- The guest vblank rate stays at the default. A 120 Hz override lifts the
  30 FPS menu screens, but in races the game then swings between 60 and
  120 FPS, which reads as stutter, and capping the host present rate on top of
  it made frame pacing worse.
- The texture budget assumes a GPU with plenty of memory. Lower the limits on
  smaller GPUs.
