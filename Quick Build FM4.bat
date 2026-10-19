@echo off
setlocal
rem Quick incremental build: only for changes to hook CODE (fm4\src). Refuses and
rem asks for the full "Build FM4.bat" when hooks/functions .toml or sdk-patches changed.
pwsh -NoProfile -File "%~dp0tools\Build-Quick.ps1" %*
pause
