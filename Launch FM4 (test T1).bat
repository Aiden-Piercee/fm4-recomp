@echo off
rem Bisect test: only the Free Play AI count is raised; cars stay capped at 16 (control for the grid pack)
set "FM4_MAX_CARS=20"
set "FM4_MAX_CARS_SKIP=maxcars,maxaicars,unified,clamp,rank"
set "FM4_CARS_CFG=%~dp0fm4_cars.cfg"
call "%~dp0Launch FM4.bat"
