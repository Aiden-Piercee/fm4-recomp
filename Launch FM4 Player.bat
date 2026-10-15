@echo off
setlocal
cd /d "%~dp0"
if not exist "game\default.xex" goto missing
if not exist "game\SpeechFacade_default.xex" goto missing
if not exist "game\XMediaFacade_default.xex" goto missing
fm4.exe --game_data_root="\\?\%~dp0game" --user_data_root="\\?\%~dp0user" --cache_root="\\?\%~dp0user\cache"
if errorlevel 1 pause
exit /b
:missing
echo Copy your own extracted FM4 Europe Game of the Year Disc 1 files into the game folder.
echo The three XEX files must be directly inside game. See README.md.
pause
exit /b 2
