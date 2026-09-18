param([string]$Destination = 'install')

$ErrorActionPreference = 'Stop'
$projectDir = [IO.Path]::GetFullPath($PSScriptRoot).TrimEnd('\')
if ([string]::IsNullOrWhiteSpace($Destination)) { throw 'Destination cannot be empty.' }
$installDir = if ([IO.Path]::IsPathRooted($Destination)) {
    [IO.Path]::GetFullPath($Destination)
} else {
    [IO.Path]::GetFullPath((Join-Path $projectDir $Destination))
}
$installDir = $installDir.TrimEnd('\')

# Keep the output away from the source and from any of its parent folders.
$comparison = [StringComparison]::OrdinalIgnoreCase
$bundleDir = Join-Path $projectDir 'bundle'
$gitDir = Join-Path $projectDir '.git'
if ($installDir.Equals($projectDir, $comparison) -or
    $projectDir.StartsWith($installDir + '\', $comparison) -or
    $installDir.Equals($bundleDir, $comparison) -or
    $installDir.StartsWith($bundleDir + '\', $comparison) -or
    $installDir.Equals($gitDir, $comparison) -or
    $installDir.StartsWith($gitDir + '\', $comparison)) {
    throw "Unsafe install destination: $installDir"
}

$files = @(
    'Tower500Lcd.cmd', 'Tower500Lcd.ps1',
    'Start LCD at Login.cmd', 'Start LCD at Login.ps1',
    'Start Dashboard.ps1', 'AppState.ps1', 'README.md',
    'LICENSE-Kraken.txt', 'LICENSE-Pillow.txt', 'LICENSE-psutil.txt',
    'LICENSE-PyInstaller.txt', 'LICENSE-Python.txt', 'LICENSE-pywinusb.txt'
)
$bundle = Join-Path $projectDir 'bundle'
if (-not (Test-Path -LiteralPath (Join-Path $bundle 'Tower500LcdProcess.exe') -PathType Leaf) -or
    -not (Test-Path -LiteralPath (Join-Path $bundle '_internal') -PathType Container)) {
    throw 'The standalone bundle is missing. Build it before creating an install folder.'
}
foreach ($file in $files) {
    if (-not (Test-Path -LiteralPath (Join-Path $projectDir $file) -PathType Leaf)) {
        throw "Required file is missing: $file"
    }
}
if (Test-Path -LiteralPath $installDir) {
    if (-not (Test-Path -LiteralPath $installDir -PathType Container) -or
        @(Get-ChildItem -LiteralPath $installDir -Force).Count -gt 0) {
        throw "Destination must be a new or empty folder: $installDir"
    }
} else {
    New-Item -ItemType Directory -Path $installDir -Force | Out-Null
}

Copy-Item -LiteralPath $bundle -Destination $installDir -Recurse -ErrorAction Stop
foreach ($file in $files) {
    Copy-Item -LiteralPath (Join-Path $projectDir $file) -Destination $installDir -ErrorAction Stop
}
$contents = @(Get-ChildItem -LiteralPath $installDir -Recurse -File)
$sizeMb = [math]::Round(($contents | Measure-Object Length -Sum).Sum / 1MB, 1)
Write-Output "Install folder ready: $installDir ($($contents.Count) files, $sizeMb MB)"
