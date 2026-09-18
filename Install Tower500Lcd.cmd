@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Install Tower500Lcd.ps1" %*
if errorlevel 1 echo Installation failed.
pause
