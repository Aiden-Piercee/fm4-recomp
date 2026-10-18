@echo off
setlocal
rem Experimental 4K (3x) performance profile for fast GPUs; see docs/PERFORMANCE.md.
rem The validated correctness profile (Launch FM4.bat) is unchanged.
pwsh -NoProfile -File "%~dp0tools\Run-Local.ps1" -GameDataRoot "%~dp0extracted" -DeferredReadbackSync -RuntimeArgs "--render_target_path_d3d12=native,--resolution_scale=3,--d3d12_readback_resolve_early_submit=true,--native_executor_stencil_ref_transfers=true,--texture_cache_memory_limit_soft=4096,--texture_cache_memory_limit_hard=8192,--texture_cache_memory_limit_soft_lifetime=600,--d3d12_texture_placed_heaps=true,--d3d12_texture_heap_prealloc_mb=1024,--readback_resolve=fast,--vblank_precise_timer=true"
