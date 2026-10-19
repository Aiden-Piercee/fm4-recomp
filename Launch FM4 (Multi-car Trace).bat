@echo off
setlocal
rem Multi-car research: file trace + [mcar] front-end/car-object trace (fm4/src/multicar_trace.cpp). Log: fm4.log
set "FM4_GAME_ROOT=%~dp0extracted"
if not exist "%FM4_GAME_ROOT%\default.xex" set "FM4_GAME_ROOT=C:\Z-Temp\Forza-4-compiling-stuff\extracted"
rem Post-launch DLC: use the repo's dlc-content folder if present, otherwise the
rem local DLC-F4 folder. Already-installed packs are skipped on later launches.
set "FM4_DLC_ARG="
set "FM4_DLC_DIR=%~dp0dlc-content"
if not exist "%FM4_DLC_DIR%\" set "FM4_DLC_DIR=C:\Z-Temp\Forza-4-compiling-stuff\game-content\DLC-F4"
if exist "%FM4_DLC_DIR%\" set "FM4_DLC_ARG=-DlcContentRoot "%FM4_DLC_DIR%""
set "FM4_MCAR_TRACE=1"
pwsh -NoProfile -File "%~dp0tools\Run-Local.ps1" -GameDataRoot "%FM4_GAME_ROOT%" %FM4_DLC_ARG% -TraceFiles
