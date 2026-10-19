@echo off
rem Experimental: lifts the 16-car limit (FM4_MAX_CARS). Needs a build with the MaxCars hooks and
rem the Le Mans 32-slot grid pack (tools\dlc\make_grid32_pack.py). Change 32 to try other counts.
set "FM4_MAX_CARS=32"
set "FM4_CARS_CFG=%~dp0fm4_cars.cfg"
call "%~dp0Launch FM4.bat"
