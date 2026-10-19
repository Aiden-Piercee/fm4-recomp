@echo off
setlocal
set "FM4_GAME_ROOT=%~dp0extracted"
if not exist "%FM4_GAME_ROOT%\default.xex" set "FM4_GAME_ROOT=C:\Z-Temp\Forza-4-compiling-stuff\extracted"
pwsh -NoProfile -File "%~dp0tools\Run-Local.ps1" -GameDataRoot "%FM4_GAME_ROOT%" -DeferredReadbackSync
