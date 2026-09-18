@echo off
start "Tower 500 LCD startup" powershell.exe -NoProfile -WindowStyle Hidden -ExecutionPolicy Bypass -File "%~dp0Start LCD at Login.ps1"
