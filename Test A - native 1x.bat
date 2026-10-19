@echo off
setlocal
cd /d "C:\Users\Me\Documents\GitHub\fm4-recomp"
set "FM4_DLC_ARG="
set "FM4_DLC_DIR=C:\Users\Me\Documents\GitHub\fm4-recomp\dlc-content"
if not exist "%FM4_DLC_DIR%\" set "FM4_DLC_DIR=C:\Z-Temp\Forza-4-compiling-stuff\game-content\DLC-F4"
if exist "%FM4_DLC_DIR%\" set "FM4_DLC_ARG=-DlcContentRoot "%FM4_DLC_DIR%""
pwsh -File .\tools\Run-Local.ps1 -GameDataRoot "C:\Z-Temp\Forza-4-compiling-stuff\extracted" -DeferredReadbackSync -RuntimeArgs "--render_target_path_d3d12=native" %FM4_DLC_ARG%
