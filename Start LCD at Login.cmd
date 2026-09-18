@echo off
start "Tower500Lcd startup" powershell.exe -NoProfile -WindowStyle Hidden -ExecutionPolicy Bypass -File "%~dp0Start LCD at Login.ps1"
