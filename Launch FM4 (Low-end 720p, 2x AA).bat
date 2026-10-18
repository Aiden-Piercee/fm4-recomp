@echo off
setlocal
rem Experimental low-end (integrated GPU) profile; see docs/PERFORMANCE.md.
rem The validated correctness profile (Launch FM4.bat) is unchanged.
pwsh -NoProfile -File "%~dp0tools\Run-Local.ps1" -GameDataRoot "%~dp0extracted" -DeferredReadbackSync -RuntimeArgs "--render_target_path_d3d12=native,--resolution_scale=1,--d3d12_readback_resolve_early_submit=true,--native_executor_stencil_ref_transfers=true,--texture_cache_memory_limit_soft=1024,--texture_cache_memory_limit_hard=1536,--texture_cache_memory_limit_soft_lifetime=120,--d3d12_texture_placed_heaps=true,--d3d12_texture_heap_prealloc_mb=256,--readback_resolve=fast,--native_executor_max_msaa_samples=2"
