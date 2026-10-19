@echo off
rem Bisect test: maxcars and maxaicars raised to 20/19; Free Play AI count left at stock 11
set "FM4_MAX_CARS=20"
set "FM4_MAX_CARS_SKIP=quick,unified,clamp,rank"
set "FM4_CARS_CFG=%~dp0fm4_cars.cfg"
call "%~dp0Launch FM4.bat"
