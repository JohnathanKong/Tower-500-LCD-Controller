# The one-folder build includes its own Python runtime and dependencies.
& (Join-Path $PSScriptRoot 'bundle\TowerLCDDisplay.exe') @args
