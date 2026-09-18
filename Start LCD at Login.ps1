param([switch]$DryRun)

. (Join-Path $PSScriptRoot 'AppState.ps1')
# The bundled executable carries Python and its required libraries.
$python = Join-Path $appDir 'bundle\TowerLCDDisplay.exe'
if (-not (Test-Path -LiteralPath $python)) { exit 1 }

$thermaltake = Get-Process -ErrorAction SilentlyContinue | Where-Object { $_.ProcessName -match '^(TTRGBPlus|TT RGB Plus)' } | Select-Object -First 1

$settings = $null
# Missing or invalid settings fall back to the defaults shown in the control app.
if (Test-Path -LiteralPath $settingsFile) {
    try { $settings = Get-Content -LiteralPath $settingsFile -Raw | ConvertFrom-Json } catch { }
}
$accent = if ($settings.accent -match '^#[0-9A-Fa-f]{6}$') { $settings.accent } else { '#28DEBB' }
$textColor = if ($settings.textColor -match '^#[0-9A-Fa-f]{6}$') { $settings.textColor } else { '#F2F7FA' }
$mutedColor = if ($settings.mutedColor -match '^#[0-9A-Fa-f]{6}$') { $settings.mutedColor } else { '#95ADBE' }
$rotation = if ($settings.rotation -in @(0, 90, 180, 270)) { [int]$settings.rotation } else { 0 }
$fit = if ($settings.fit -eq 'contain') { 'contain' } else { 'cover' }
$interval = if ($settings.interval -ge 0.5) { [double]$settings.interval } else { 2 }
$brightness = if ($null -ne $settings.brightness -and [double]$settings.brightness -ge 0 -and [double]$settings.brightness -le 100) { [int]$settings.brightness } else { 100 }

$arguments = @('--accent', $accent, '--text-color', $textColor, '--muted-color', $mutedColor, '--rotation', "$rotation", '--interval', "$interval", '--brightness', "$brightness", '--settings-file', ('"{0}"' -f $settingsFile))
$imagePath = if ($settings.image -and [IO.Path]::IsPathRooted([string]$settings.image)) { [string]$settings.image } elseif ($settings.image) { Join-Path $dataDir ([string]$settings.image) } else { $null }
if ($imagePath -and -not (Test-Path -LiteralPath $imagePath -PathType Leaf) -and $settings.image -and -not [IO.Path]::IsPathRooted([string]$settings.image)) { $imagePath = Join-Path $appDir ([string]$settings.image) }
if ($imagePath -and (Test-Path -LiteralPath $imagePath -PathType Leaf)) {
    $arguments += @('--background', ('"{0}"' -f $imagePath), '--fit', $fit)
}
if ($DryRun) { Write-Output ($arguments -join ' '); exit 0 }
# Serialize launch attempts, then replace workers from old or new install paths.
$launchMutex = [Threading.Mutex]::new($false, 'Local\Tower500LCD_WorkerLaunch')
$launchMutex.WaitOne() | Out-Null
try {
    foreach ($existing in @(Get-Process -Name TowerLCDDisplay -ErrorAction SilentlyContinue)) {
        $existing.Kill()
        $existing.WaitForExit(3000) | Out-Null
        if (-not $existing.HasExited) { throw "Could not stop old display process $($existing.Id)" }
    }
    if (Test-Path -LiteralPath $pidFile) { Remove-Item -LiteralPath $pidFile -ErrorAction SilentlyContinue }
    if (-not $thermaltake) {
        # Detached workers need output files; inherited console pipes close with this launcher.
        $process = Start-Process -FilePath $python -ArgumentList $arguments -WorkingDirectory $appDir -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $dataDir 'dashboard.log') -RedirectStandardError (Join-Path $dataDir 'dashboard-error.log')
        @{ pid = $process.Id; startUtcTicks = $process.StartTime.ToUniversalTime().Ticks } |
            ConvertTo-Json | Set-Content -LiteralPath $pidFile -Encoding UTF8
    }
} finally {
    $launchMutex.ReleaseMutex()
    $launchMutex.Dispose()
}
