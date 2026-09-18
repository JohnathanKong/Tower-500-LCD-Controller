# Run the display in this PowerShell session, using Codex's Python library.
$runtime = 'C:\Users\User\.cache\codex-runtimes\codex-primary-runtime\dependencies\python'
$env:PYTHONHOME = $runtime
$env:PATH = "$runtime;$env:PATH"
$env:PYTHONPATH = $PSScriptRoot
& (Join-Path $PSScriptRoot 'TowerLCDDisplay.exe') (Join-Path $PSScriptRoot 'tower_display.py') @args
