@echo off
rem Bisect test: 20 cars but leave maxcarstoloadunified at stock.
set "FM4_MAX_CARS=20"
set "FM4_MAX_CARS_SKIP=unified"
set "FM4_CARS_CFG=%~dp0fm4_cars.cfg"
call "%~dp0Launch FM4.bat"
