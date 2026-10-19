@echo off
rem Experimental car-count test (FM4_MAX_CARS=28). See "Launch FM4 (32 cars).bat".
set "FM4_MAX_CARS=28"
set "FM4_CARS_CFG=%~dp0fm4_cars.cfg"
call "%~dp0Launch FM4.bat"
