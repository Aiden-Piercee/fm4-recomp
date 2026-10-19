@echo off
setlocal

if "%~1"=="" (
  echo Usage: %~nx0 ^<folder-containing-extracted-Disc-2-packages^>
  exit /b 2
)

set "FM4_DISC2_ROOT=%~f1"
if not exist "%FM4_DISC2_ROOT%\" (
  echo Disc 2 package folder not found: %FM4_DISC2_ROOT%
  exit /b 2
)

rem Disc 1 game data: use the repo's extracted folder if it has default.xex,
rem otherwise fall back to the folder passed in (Disc 1 + Disc 2 in one place).
set "FM4_GAME_ROOT=%~dp0extracted"
if not exist "%FM4_GAME_ROOT%\default.xex" set "FM4_GAME_ROOT=%FM4_DISC2_ROOT%"

pwsh -NoProfile -File "%~dp0tools\Run-Local.ps1" -GameDataRoot "%FM4_GAME_ROOT%" -Disc2ContentRoot "%FM4_DISC2_ROOT%"
