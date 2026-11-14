param(
    [string]$Destination = (Join-Path $env:ProgramFiles 'Thermaltake500LCD'),
    [switch]$NoStartup,
    [switch]$Elevated
)
$ErrorActionPreference = 'Stop'
$source = $PSScriptRoot
$gui = Join-Path $source 'thermaltake500lcd.exe'
$worker = Join-Path $source 'thermaltake500lcd.service.exe'
if (-not (Test-Path -LiteralPath $gui -PathType Leaf) -or -not (Test-Path -LiteralPath $worker -PathType Leaf)) {
    throw 'Both C++ executables must be beside Install.ps1. Download the Windows-x64 build artifact or build with CMake.'
}
$Destination = [IO.Path]::GetFullPath($Destination)
$sourcePath = [IO.Path]::GetFullPath($source).TrimEnd('\')
$driveRoot = [IO.Path]::GetPathRoot($Destination).TrimEnd('\')
if ($Destination.TrimEnd('\') -eq $driveRoot -or
    $Destination.TrimEnd('\') -eq $sourcePath -or
    $sourcePath.StartsWith($Destination.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Unsafe install destination.'
}
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$admin = [Security.Principal.WindowsPrincipal]::new($identity).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
$programFiles = [IO.Path]::GetFullPath($env:ProgramFiles).TrimEnd('\')
if ($Destination.StartsWith($programFiles + '\', [StringComparison]::OrdinalIgnoreCase) -and -not $admin) {
    if ($Elevated) { throw 'Administrator permission is needed to install under Program Files.' }
    $args = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
        ('"{0}"' -f $PSCommandPath), '-Destination', ('"{0}"' -f $Destination), '-Elevated')
    if ($NoStartup) { $args += '-NoStartup' }
    $process = Start-Process powershell.exe -Verb RunAs -Wait -PassThru -ArgumentList $args
    if ($process.ExitCode -ne 0) { throw "Elevated installer failed (exit $($process.ExitCode))." }
    return
}
New-Item -ItemType Directory -Path $Destination -Force | Out-Null
$destinationWorker = Join-Path $Destination 'thermaltake500lcd.service.exe'
# Ask the native worker to release the USB panel (do not kill processes by name).
Add-Type -Namespace NativeLCD -Name Signal -MemberDefinition @'
[DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
public static extern System.IntPtr OpenEvent(uint access, bool inherit, string name);
[DllImport("kernel32.dll", SetLastError = true)]
public static extern bool SetEvent(System.IntPtr handle);
[DllImport("kernel32.dll", SetLastError = true)]
public static extern bool CloseHandle(System.IntPtr handle);
'@
$eventHandle = [NativeLCD.Signal]::OpenEvent(0x0002, $false, 'Local\Thermaltake500LCD.Stop')
if ($eventHandle -ne [IntPtr]::Zero) {
    [void][NativeLCD.Signal]::SetEvent($eventHandle)
    [void][NativeLCD.Signal]::CloseHandle($eventHandle)
}
# Stop only recognized old or current LCD executables, not arbitrary Python programs.
$oldNames = @('Tower500LcdProcess', 'TowerLCDDisplay', 'thermaltake500lcd.service')
foreach ($process in @(Get-Process -Name $oldNames -ErrorAction SilentlyContinue)) {
    try {
        $exe = $process.Path
        $isOld = $exe -and $exe.EndsWith('\Tower500LcdProcess.exe', [StringComparison]::OrdinalIgnoreCase)
        $isLegacy = $exe -and $exe.EndsWith('\TowerLCDDisplay.exe', [StringComparison]::OrdinalIgnoreCase)
        $isNew = $exe -and $exe.Equals($destinationWorker, [StringComparison]::OrdinalIgnoreCase)
        if ($isOld -or $isLegacy -or $isNew) {
            if (-not $process.WaitForExit(4000)) {
                $process.Kill()
                if (-not $process.WaitForExit(3000)) { throw "Could not stop LCD process $($process.Id)" }
            }
        }
    } catch {
        throw "Could not stop an older LCD process: $($_.Exception.Message)"
    }
}
Copy-Item -LiteralPath $gui -Destination $Destination -Force
Copy-Item -LiteralPath $worker -Destination $Destination -Force
Copy-Item -LiteralPath (Join-Path $source 'README.md') -Destination $Destination -Force

# Replace old auto-start shortcut, without altering the user's source or images.
$startup = [Environment]::GetFolderPath('Startup')
$shell = New-Object -ComObject WScript.Shell
foreach ($link in (Get-ChildItem -LiteralPath $startup -Filter '*.lnk' -File -ErrorAction SilentlyContinue)) {
    try {
        $shortcut = $shell.CreateShortcut($link.FullName)
        if ($shortcut.TargetPath -match '(?i)Start LCD at Login\.cmd$') {
            Rename-Item -LiteralPath $link.FullName -NewName ($link.Name + '.disabled') -ErrorAction Stop
            Write-Output "Disabled legacy LCD startup shortcut: $($link.Name)"
        }
    } catch {
        Write-Warning "Could not inspect an existing Startup shortcut: $($link.Name)"
    }
}
$newLink = Join-Path $startup 'Thermaltake500LCD.lnk'
if ($NoStartup) {
    if (Test-Path -LiteralPath $newLink) { Remove-Item -LiteralPath $newLink }
} else {
    $shortcut = $shell.CreateShortcut($newLink)
    $shortcut.TargetPath = $destinationWorker
    $shortcut.Arguments = '--run'
    $shortcut.WorkingDirectory = $Destination
    $shortcut.Description = 'Start the Thermaltake LCD dashboard when signing in'
    $shortcut.Save()
}
Write-Output "Installed native controller to $Destination"
Write-Output "Your original image library and settings remain under $env:LOCALAPPDATA\Tower500LCD"
Write-Output "Open $(Join-Path $Destination 'thermaltake500lcd.exe') to configure and start the screen."
if (-not $NoStartup) { Write-Output 'The C++ background worker will start on your next sign-in.' }
