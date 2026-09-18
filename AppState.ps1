# Code may live in Program Files; all changing files belong in the user's profile.
$appDir = $PSScriptRoot
$dataDir = Join-Path $env:LOCALAPPDATA 'Tower500LCD'
$libraryDir = Join-Path $dataDir 'images'
$settingsFile = Join-Path $dataDir 'lcd-settings.json'
$pidFile = Join-Path $dataDir 'dashboard.pid'
$uiPidFile = Join-Path $dataDir 'control.pid'
New-Item -ItemType Directory -Path $libraryDir -Force | Out-Null

# Bring settings and images forward when an existing installation is moved.
$oldSettings = Join-Path $appDir 'lcd-settings.json'
if (-not (Test-Path -LiteralPath $settingsFile) -and (Test-Path -LiteralPath $oldSettings)) {
    Copy-Item -LiteralPath $oldSettings -Destination $settingsFile
}
$oldLibrary = Join-Path $appDir 'images'
if (Test-Path -LiteralPath $oldLibrary) {
    Get-ChildItem -LiteralPath $oldLibrary -File | ForEach-Object {
        $destination = Join-Path $libraryDir $_.Name
        if (-not (Test-Path -LiteralPath $destination)) { Copy-Item -LiteralPath $_.FullName -Destination $destination }
    }
}
