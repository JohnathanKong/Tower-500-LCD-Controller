param([string]$PythonExe = 'python.exe')

$ErrorActionPreference = 'Stop'
$appDir = $PSScriptRoot
$buildTools = Join-Path $appDir '.build-deps'
$oldPythonPath = $env:PYTHONPATH
try {
    # Vendored psutil and pywinusb are imported from this repository.
    $env:PYTHONPATH = "$appDir;$buildTools"
    & $PythonExe -c 'import PIL, PyInstaller, psutil, pywinusb.hid'
    if ($LASTEXITCODE -ne 0) {
        throw 'Build dependencies are missing. See the README build instructions.'
    }
    & $PythonExe -m PyInstaller --noconfirm --clean --onedir --console `
        --name Tower500LcdProcess `
        --version-file (Join-Path $appDir 'version-info.txt') `
        --paths $appDir `
        --additional-hooks-dir (Join-Path $appDir 'hooks') `
        --exclude-module numpy `
        --distpath (Join-Path $appDir '.build-dist') `
        --workpath (Join-Path $appDir '.build-work') `
        --specpath (Join-Path $appDir '.build-spec') `
        (Join-Path $appDir 'tower_display.py')
    if ($LASTEXITCODE -ne 0) { throw 'Standalone build failed.' }
    Write-Output "Built: $(Join-Path $appDir '.build-dist\Tower500LcdProcess')"
} finally {
    $env:PYTHONPATH = $oldPythonPath
}
