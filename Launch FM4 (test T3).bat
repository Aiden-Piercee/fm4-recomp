@echo off
rem Bisect test: maxcars and Free Play AI count raised; maxaicars left at stock 11
set "FM4_MAX_CARS=20"
set "FM4_MAX_CARS_SKIP=maxaicars,unified,clamp,rank"
set "FM4_CARS_CFG=%~dp0fm4_cars.cfg"
call "%~dp0Launch FM4.bat"
