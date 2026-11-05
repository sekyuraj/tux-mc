@echo off
rem Double-click this to install tux-mc. It runs install.ps1 next to it.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0install.ps1" %*
if errorlevel 1 echo. & echo Something went wrong, see the message above.
pause
