@echo off
rem Experimental: asks how many cars (17-48) and runs with FM4_MAX_CARS set to it. Leaves maxcarstoloadunified at stock.
set /p FM4_MAX_CARS=Total cars (player + AI), e.g. 17: 
set "FM4_MAX_CARS_SKIP=unified"
set "FM4_CARS_CFG=%~dp0fm4_cars.cfg"
call "%~dp0Launch FM4.bat"
