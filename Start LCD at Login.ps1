param([switch]$DryRun)

$appDir = $PSScriptRoot
# The small display executable is a renamed Python interpreter.
$runtime = 'C:\Users\User\.cache\codex-runtimes\codex-primary-runtime\dependencies\python'
$env:PYTHONHOME = $runtime
$env:PATH = "$runtime;$env:PATH"
$python = Join-Path $appDir 'TowerLCDDisplay.exe'
$driver = Join-Path $appDir 'tower_display.py'
$settingsFile = Join-Path $appDir 'lcd-settings.json'
$pidFile = Join-Path $appDir 'dashboard.pid'
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

$arguments = @($driver, '--accent', $accent, '--text-color', $textColor, '--muted-color', $mutedColor, '--rotation', "$rotation", '--interval', "$interval", '--brightness', "$brightness", '--settings-file', ('"{0}"' -f $settingsFile))
$imagePath = if ($settings.image -and [IO.Path]::IsPathRooted([string]$settings.image)) { [string]$settings.image } elseif ($settings.image) { Join-Path $appDir ([string]$settings.image) } else { $null }
if ($imagePath -and (Test-Path -LiteralPath $imagePath -PathType Leaf)) {
    $arguments += @('--background', ('"{0}"' -f $imagePath), '--fit', $fit)
}
if ($DryRun) { Write-Output ($arguments -join ' '); exit 0 }
# Serialize launch attempts, then replace every worker from this app folder.
$launchMutex = [Threading.Mutex]::new($false, 'Local\Tower500LCD_WorkerLaunch')
$launchMutex.WaitOne() | Out-Null
try {
    foreach ($existing in @(Get-Process -Name TowerLCDDisplay -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $python })) {
        $existing.Kill()
        $existing.WaitForExit(3000) | Out-Null
        if (-not $existing.HasExited) { throw "Could not stop old display process $($existing.Id)" }
    }
    if (Test-Path -LiteralPath $pidFile) { Remove-Item -LiteralPath $pidFile -ErrorAction SilentlyContinue }
    if (-not $thermaltake) {
        # Detached workers need output files; inherited console pipes close with this launcher.
        $process = Start-Process -FilePath $python -ArgumentList $arguments -WorkingDirectory $appDir -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $appDir 'dashboard.log') -RedirectStandardError (Join-Path $appDir 'dashboard-error.log')
        @{ pid = $process.Id; startUtcTicks = $process.StartTime.ToUniversalTime().Ticks } |
            ConvertTo-Json | Set-Content -LiteralPath $pidFile -Encoding UTF8
    }
} finally {
    $launchMutex.ReleaseMutex()
    $launchMutex.Dispose()
}
