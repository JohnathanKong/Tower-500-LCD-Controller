param(
    [string]$Destination = (Join-Path ([Environment]::GetFolderPath('ProgramFiles')) 'Tower500Lcd'),
    [switch]$Elevated
)

$ErrorActionPreference = 'Stop'
$projectDir = [IO.Path]::GetFullPath($PSScriptRoot).TrimEnd('\')
if ([string]::IsNullOrWhiteSpace($Destination)) { throw 'Destination cannot be empty.' }
$installDir = if ([IO.Path]::IsPathRooted($Destination)) {
    [IO.Path]::GetFullPath($Destination)
} else {
    [IO.Path]::GetFullPath((Join-Path $projectDir $Destination))
}
$installDir = $installDir.TrimEnd('\')
$comparison = [StringComparison]::OrdinalIgnoreCase
$driveRoot = [IO.Path]::GetPathRoot($installDir).TrimEnd('\')
$programFiles = [IO.Path]::GetFullPath([Environment]::GetFolderPath('ProgramFiles')).TrimEnd('\')
$sourceBundleDir = Join-Path $projectDir 'bundle'
$gitDir = Join-Path $projectDir '.git'
if ($installDir.Equals($driveRoot, $comparison) -or
    $installDir.Equals($programFiles, $comparison) -or
    $installDir.Equals($projectDir, $comparison) -or
    $projectDir.StartsWith($installDir + '\', $comparison) -or
    $installDir.Equals($sourceBundleDir, $comparison) -or
    $installDir.StartsWith($sourceBundleDir + '\', $comparison) -or
    $installDir.Equals($gitDir, $comparison) -or
    $installDir.StartsWith($gitDir + '\', $comparison)) {
    throw "Unsafe install destination: $installDir"
}

# A normal double-click can install to Program Files after the Windows UAC prompt.
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$isAdmin = [Security.Principal.WindowsPrincipal]::new($identity).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
$inProgramFiles = $installDir.StartsWith($programFiles + '\', $comparison)
if ($inProgramFiles -and -not $isAdmin) {
    if ($Elevated) { throw 'Administrator permission is required for Program Files.' }
    $arguments = @(
        '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
        ('"{0}"' -f $PSCommandPath), '-Destination', ('"{0}"' -f $installDir), '-Elevated'
    )
    try {
        $child = Start-Process -FilePath 'powershell.exe' -Verb RunAs -ArgumentList $arguments -Wait -PassThru
    } catch {
        throw "Installation was cancelled or elevation failed: $($_.Exception.Message)"
    }
    if ($child.ExitCode -ne 0) { throw "Elevated installer failed with exit code $($child.ExitCode)." }
    return
}

$files = @(
    'Tower500Lcd.cmd', 'Tower500Lcd.ps1',
    'Start LCD at Login.cmd', 'Start LCD at Login.ps1',
    'Start Dashboard.ps1', 'AppState.ps1', 'README.md',
    'LICENSE-Kraken.txt', 'LICENSE-Pillow.txt', 'LICENSE-psutil.txt',
    'LICENSE-PyInstaller.txt', 'LICENSE-Python.txt', 'LICENSE-pywinusb.txt'
)
$sourceBundle = Join-Path $projectDir 'bundle'
if (-not (Test-Path -LiteralPath (Join-Path $sourceBundle 'Tower500LcdProcess.exe') -PathType Leaf) -or
    -not (Test-Path -LiteralPath (Join-Path $sourceBundle '_internal') -PathType Container)) {
    throw 'The standalone bundle is missing. Build it before installing.'
}
foreach ($file in $files) {
    if (-not (Test-Path -LiteralPath (Join-Path $projectDir $file) -PathType Leaf)) {
        throw "Required file is missing: $file"
    }
}
if (Test-Path -LiteralPath $installDir -PathType Leaf) { throw "Destination is a file: $installDir" }
if (Test-Path -LiteralPath $installDir -PathType Container) {
    $existingDestination = Get-Item -LiteralPath $installDir -Force
    if ($existingDestination.Attributes -band [IO.FileAttributes]::ReparsePoint) {
        throw 'Destination is a link; choose a real folder for installation.'
    }
}
New-Item -ItemType Directory -Path $installDir -Force | Out-Null

# An upgrade must release the old executable before replacing its bundle.
$workerPaths = @(
    (Join-Path $installDir 'bundle\Tower500LcdProcess.exe'),
    (Join-Path $installDir 'bundle\TowerLCDDisplay.exe'),
    (Join-Path $installDir 'TowerLCDDisplay.exe')
)
foreach ($process in @(Get-Process -Name Tower500LcdProcess,TowerLCDDisplay -ErrorAction SilentlyContinue)) {
    try {
        if ($process.Path -and @($workerPaths | Where-Object { $_.Equals($process.Path, $comparison) }).Count -gt 0) {
            $process.Kill()
            $process.WaitForExit(5000) | Out-Null
            if (-not $process.HasExited) { throw "Could not stop process $($process.Id)." }
        }
    } catch [System.ComponentModel.Win32Exception] {
        throw "Could not stop the running display process: $($_.Exception.Message)"
    }
}

$installedBundle = Join-Path $installDir 'bundle'
if (Test-Path -LiteralPath $installedBundle) {
    $item = Get-Item -LiteralPath $installedBundle -Force
    if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'The installed bundle is a link; remove it manually before upgrading.' }
    $expected = [IO.Path]::GetFullPath($installedBundle)
    if (-not $expected.StartsWith($installDir + '\', $comparison)) { throw 'Unexpected bundle path.' }
    Remove-Item -LiteralPath $installedBundle -Recurse -Force
}
Copy-Item -LiteralPath $sourceBundle -Destination $installDir -Recurse -ErrorAction Stop
foreach ($file in $files) {
    Copy-Item -LiteralPath (Join-Path $projectDir $file) -Destination $installDir -Force -ErrorAction Stop
}
# Remove files from the older naming scheme when upgrading an existing copy.
foreach ($oldName in @('Open LCD App.cmd', 'Open LCD App.ps1', 'TowerLCDDisplay.exe')) {
    $oldPath = Join-Path $installDir $oldName
    if (Test-Path -LiteralPath $oldPath -PathType Leaf) { Remove-Item -LiteralPath $oldPath -Force }
}
$contents = @(Get-ChildItem -LiteralPath $installDir -Recurse -File)
$sizeMb = [math]::Round(($contents | Measure-Object Length -Sum).Sum / 1MB, 1)
Write-Output "Installed Tower500Lcd to $installDir ($($contents.Count) files, $sizeMb MB)."
Write-Output "Open $(Join-Path $installDir 'Tower500Lcd.cmd') to control the display."
