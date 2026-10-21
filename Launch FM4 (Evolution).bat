@echo off
setlocal
rem Evolution testing: same as "Launch FM4.bat" but WITHOUT the post-launch DLC
rem install, so only the packs already in .local\user (Evolution + Damage Plus)
rem are used and nothing from DLC-F4 gets reinstalled.
cd /d "%~dp0"
pwsh -File .\tools\Run-Local.ps1 -GameDataRoot "C:\Z-Temp\Forza-4-compiling-stuff\extracted" -DeferredReadbackSync -RuntimeArgs "--render_target_path_d3d12=native,--resolution_scale=2"
