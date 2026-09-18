Add-Type -AssemblyName PresentationFramework
Add-Type -AssemblyName PresentationCore

$appDir = $PSScriptRoot
# The renamed executable still loads Python's standard library from this runtime.
$runtime = 'C:\Users\User\.cache\codex-runtimes\codex-primary-runtime\dependencies\python'
$env:PYTHONHOME = $runtime
$env:PATH = "$runtime;$env:PATH"
$python = Join-Path $appDir 'TowerLCDDisplay.exe'
$script = Join-Path $appDir 'tower_display.py'
$pidFile = Join-Path $appDir 'dashboard.pid'
$uiPidFile = Join-Path $appDir 'control.pid'
$settingsFile = Join-Path $appDir 'lcd-settings.json'
$libraryDir = Join-Path $appDir 'images'
New-Item -ItemType Directory -Path $libraryDir -Force | Out-Null

function Import-Image([string]$source) {
    # Store a content-named copy so deleting the original never breaks the LCD.
    $sourcePath = (Resolve-Path -LiteralPath $source -ErrorAction Stop).Path
    if ([IO.Path]::GetDirectoryName($sourcePath) -eq $libraryDir) { return $sourcePath }
    $fullHash = (Get-FileHash -LiteralPath $sourcePath -Algorithm SHA256).Hash
    foreach ($existing in (Get-ChildItem -LiteralPath $libraryDir -File)) {
        if ((Get-FileHash -LiteralPath $existing.FullName -Algorithm SHA256).Hash -eq $fullHash) {
            return $existing.FullName
        }
    }
    $hash = $fullHash.Substring(0, 10)
    $stem = [IO.Path]::GetFileNameWithoutExtension($sourcePath)
    if ($stem.Length -gt 48) { $stem = $stem.Substring(0, 48) }
    $extension = [IO.Path]::GetExtension($sourcePath).ToLowerInvariant()
    $destination = Join-Path $libraryDir ("$stem-$hash$extension")
    if (-not (Test-Path -LiteralPath $destination)) { Copy-Item -LiteralPath $sourcePath -Destination $destination -ErrorAction Stop }
    return $destination
}

function Resolve-SavedImage([string]$saved) {
    if (-not $saved) { return $null }
    $path = if ([IO.Path]::IsPathRooted($saved)) { $saved } else { Join-Path $appDir $saved }
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { return $null }
    return Import-Image $path
}

# A new control window replaces the previous one, including older versions
# that did not yet write control.pid.
$uiMutex = [Threading.Mutex]::new($false, 'Local\Tower500LCD_ControlLaunch')
$uiMutex.WaitOne() | Out-Null
try {
    if (Test-Path -LiteralPath $uiPidFile) {
        try {
            $old = Get-Content -LiteralPath $uiPidFile -Raw | ConvertFrom-Json
            $oldProcess = Get-Process -Id ([int]$old.pid) -ErrorAction Stop
            if ($oldProcess.Id -ne $PID -and $oldProcess.ProcessName -in @('powershell', 'pwsh') -and $oldProcess.StartTime.ToUniversalTime().Ticks -eq [long]$old.startUtcTicks) {
                $oldProcess.Kill()
                $oldProcess.WaitForExit(3000) | Out-Null
            }
        } catch { }
    }
    Get-Process -Name powershell,pwsh -ErrorAction SilentlyContinue | Where-Object { $_.Id -ne $PID -and $_.MainWindowTitle -eq 'Tower 500 LCD' } | ForEach-Object {
        try { $_.Kill(); $_.WaitForExit(3000) | Out-Null } catch { }
    }
    $self = Get-Process -Id $PID
    @{ pid = $PID; startUtcTicks = $self.StartTime.ToUniversalTime().Ticks } | ConvertTo-Json | Set-Content -LiteralPath $uiPidFile -Encoding UTF8
} finally {
    $uiMutex.ReleaseMutex()
    $uiMutex.Dispose()
}

$selectedImage = $null
$accent = '#28DEBB'
$textColor = '#F2F7FA'
$mutedColor = '#95ADBE'
$savedSettings = $null
if (Test-Path -LiteralPath $settingsFile) {
    try { $savedSettings = Get-Content -LiteralPath $settingsFile -Raw | ConvertFrom-Json } catch { $savedSettings = $null }
}
if ($savedSettings) {
    if ($savedSettings.accent -match '^#[0-9A-Fa-f]{6}$') { $accent = $savedSettings.accent }
    if ($savedSettings.textColor -match '^#[0-9A-Fa-f]{6}$') { $textColor = $savedSettings.textColor }
    if ($savedSettings.mutedColor -match '^#[0-9A-Fa-f]{6}$') { $mutedColor = $savedSettings.mutedColor }
    $selectedImage = Resolve-SavedImage ([string]$savedSettings.image)
}

$xaml = @'
<Window xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
        Title="Tower 500 LCD" Width="540" Height="605" ResizeMode="NoResize"
        WindowStartupLocation="CenterScreen" Background="#101A24">
  <StackPanel Margin="22">
    <TextBlock Text="Thermaltake 3.9-inch LCD" Foreground="#F2F7FA" FontSize="22" FontWeight="Bold"/>
    <TextBlock Text="480 &#215; 128  &#183;  USB 264A:233D" Foreground="#9CB8C7" Margin="0,4,0,16"/>
    <TextBlock Text="GPU Load: NVIDIA GPU 0 (RTX 5090)" Foreground="#9CB8C7" Margin="0,-10,0,12"/>
    <Border BorderBrush="#345466" BorderThickness="1">
      <Image Name="Preview" Width="480" Height="128" Stretch="Fill"/>
    </Border>
    <WrapPanel Margin="0,16,0,0">
      <Button Name="Start" Content="Start live dashboard" Padding="10,7" Margin="0,0,8,0"/>
      <Button Name="Stop" Content="Stop" Padding="10,7" Margin="0,0,8,0"/>
    </WrapPanel>
    <WrapPanel Margin="0,8,0,0">
      <Button Name="Choose" Content="Import image&#8230;" Padding="10,7" Margin="0,0,8,0"/>
      <Button Name="Clear" Content="Clear background" Padding="10,7" Margin="0,0,8,0"/>
      <Button Name="Send" Content="Send still image only" Padding="10,7"/>
    </WrapPanel>
    <WrapPanel Margin="0,8,0,0" VerticalAlignment="Center">
      <TextBlock Text="Image library" Foreground="#F2F7FA" VerticalAlignment="Center" Margin="0,0,10,0"/>
      <ComboBox Name="ImageLibrary" Width="380"/>
    </WrapPanel>
    <WrapPanel Margin="0,12,0,0" VerticalAlignment="Center">
      <TextBlock Text="Rotate" Foreground="#F2F7FA" VerticalAlignment="Center" Margin="0,0,8,0"/>
      <ComboBox Name="Rotation" Width="80" SelectedIndex="0" Margin="0,0,16,0">
        <ComboBoxItem Content="0&#176;" Tag="0"/>
        <ComboBoxItem Content="90&#176;" Tag="90"/>
        <ComboBoxItem Content="180&#176;" Tag="180"/>
        <ComboBoxItem Content="270&#176;" Tag="270"/>
      </ComboBox>
      <TextBlock Text="Refresh every" Foreground="#F2F7FA" VerticalAlignment="Center"/>
      <TextBox Name="Interval" Text="2" Width="36" Margin="8,0,5,0" VerticalContentAlignment="Center"/>
      <TextBlock Text="seconds" Foreground="#F2F7FA" VerticalAlignment="Center"/>
    </WrapPanel>
    <WrapPanel Margin="0,10,0,0">
      <TextBlock Text="Image fit" Foreground="#F2F7FA" VerticalAlignment="Center" Margin="0,0,8,0"/>
      <ComboBox Name="Fit" Width="165" SelectedIndex="0">
        <ComboBoxItem Content="Crop to fill" Tag="cover"/>
        <ComboBoxItem Content="Show whole image" Tag="contain"/>
      </ComboBox>
    </WrapPanel>
    <WrapPanel Margin="0,12,0,0" VerticalAlignment="Center">
      <TextBlock Text="Brightness" Foreground="#F2F7FA" VerticalAlignment="Center" Margin="0,0,10,0"/>
      <Slider Name="Brightness" Minimum="0" Maximum="100" Value="100" Width="305" TickFrequency="1" IsSnapToTickEnabled="True" VerticalAlignment="Center"/>
      <TextBlock Name="BrightnessValue" Text="100%" Width="48" Foreground="#F2F7FA" Margin="10,0,0,0" VerticalAlignment="Center"/>
    </WrapPanel>
    <TextBlock Text="Dims the image; the LCD backlight stays on." Foreground="#9CB8C7" FontSize="11" Margin="0,3,0,0"/>
    <WrapPanel Margin="0,12,0,0">
      <Button Name="AccentColor" Content="Accent color&#8230;" Padding="8,5" Margin="0,0,8,0"/>
      <Button Name="TextColor" Content="Main text color&#8230;" Padding="8,5"/>
    </WrapPanel>
    <TextBlock Name="Status" Foreground="#28DEBB" Margin="0,16,0,0"/>
  </StackPanel>
</Window>
'@

$reader = [System.Xml.XmlReader]::Create([System.IO.StringReader]::new($xaml))
$window = [Windows.Markup.XamlReader]::Load($reader)
$preview = $window.FindName('Preview')
$status = $window.FindName('Status')
$interval = $window.FindName('Interval')
$rotation = $window.FindName('Rotation')
$fit = $window.FindName('Fit')
$imageLibrary = $window.FindName('ImageLibrary')
$brightness = $window.FindName('Brightness')
$brightnessValue = $window.FindName('BrightnessValue')
if ($savedSettings) {
    if ($savedSettings.rotation -in @(0, 90, 180, 270)) { $rotation.SelectedIndex = [array]::IndexOf(@(0, 90, 180, 270), [int]$savedSettings.rotation) }
    if ($savedSettings.fit -eq 'contain') { $fit.SelectedIndex = 1 }
    if ($savedSettings.interval -ge 0.5) { $interval.Text = [string]$savedSettings.interval }
    if ($null -ne $savedSettings.brightness -and [double]$savedSettings.brightness -ge 0 -and [double]$savedSettings.brightness -le 100) {
        $brightness.Value = [double]$savedSettings.brightness
    }
}
$brightnessValue.Text = '{0:0}%' -f $brightness.Value

function Get-Rotation { return [int]$rotation.SelectedItem.Tag }
function Get-Fit { return [string]$fit.SelectedItem.Tag }

function Save-Settings {
    $savedImage = if ($selectedImage) { Join-Path 'images' ([IO.Path]::GetFileName($selectedImage)) } else { $null }
    @{ rotation = Get-Rotation; fit = Get-Fit; interval = $interval.Text; accent = $accent; textColor = $textColor; mutedColor = $mutedColor; image = $savedImage; brightness = [int]$brightness.Value } |
        ConvertTo-Json | Set-Content -LiteralPath $settingsFile -Encoding UTF8
}

function Refresh-Library {
    $imageLibrary.Items.Clear()
    foreach ($file in (Get-ChildItem -LiteralPath $libraryDir -File | Where-Object { $_.Extension.ToLowerInvariant() -in @('.png', '.jpg', '.jpeg', '.bmp', '.webp') } | Sort-Object Name)) {
        $item = [System.Windows.Controls.ComboBoxItem]::new()
        $item.Content = $file.Name -replace '-[0-9A-Fa-f]{10}(?=\.)', ''
        $item.Tag = $file.FullName
        [void]$imageLibrary.Items.Add($item)
        if ($selectedImage -eq $file.FullName) { $imageLibrary.SelectedItem = $item }
    }
}

function Get-DashboardProcess {
    # A PID can be reused by Windows, so require both the process name and start time.
    if (Test-Path -LiteralPath $pidFile) {
        try {
            $record = Get-Content -LiteralPath $pidFile -Raw | ConvertFrom-Json
            $process = Get-Process -Id ([int]$record.pid) -ErrorAction Stop
            if ($process.ProcessName -eq 'TowerLCDDisplay' -and $process.Path -eq $python -and $process.StartTime.ToUniversalTime().Ticks -eq [long]$record.startUtcTicks) { return $process }
        } catch { }
    }
    # A sign-in launch can replace the worker while this window stays open.
    return Get-Process -Name TowerLCDDisplay -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $python } | Select-Object -First 1
}

function Stop-Dashboard {
    # Stop every worker from this app folder, even if a PID record went stale.
    try {
        $workers = @(Get-Process -Name TowerLCDDisplay -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $python })
        foreach ($process in $workers) {
            $process.Kill()
            $process.WaitForExit(3000) | Out-Null
            if (-not $process.HasExited) { throw 'The display process did not stop within three seconds.' }
        }
        if (Test-Path -LiteralPath $pidFile) { Remove-Item -LiteralPath $pidFile -ErrorAction Stop }
        $status.Text = 'Stopped. The last frame remains on the LCD.'
        return $true
    } catch {
        $message = $_.Exception.Message
        $status.Text = 'Could not stop the dashboard.'
        [System.Windows.MessageBox]::Show("Could not stop the dashboard.`n$message", 'Tower 500 LCD') | Out-Null
        return $false
    }
}

function Show-Preview {
    # Preview mode writes a PNG and never opens the USB panel. Reload the bytes
    # instead of the URI because WPF otherwise reuses its cached old frame.
    $path = Join-Path $appDir 'preview.png'
    $degrees = Get-Rotation
    if ($selectedImage) {
        $path = Join-Path $appDir 'selected-preview.png'
        & $python $script --background $selectedImage --accent $accent --text-color $textColor --muted-color $mutedColor --rotation $degrees --fit (Get-Fit) --brightness ([int]$brightness.Value) --preview $path | Out-Null
    } else {
        & $python $script --accent $accent --text-color $textColor --muted-color $mutedColor --rotation $degrees --brightness ([int]$brightness.Value) --preview $path | Out-Null
    }
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $path)) {
        $status.Text = 'Preview could not be updated. Check the selected image.'
        return
    }
    $bytes = [System.IO.File]::ReadAllBytes($path)
    $stream = [System.IO.MemoryStream]::new($bytes, $false)
    $bitmap = [System.Windows.Media.Imaging.BitmapImage]::new()
    $bitmap.BeginInit()
    $bitmap.CacheOption = [System.Windows.Media.Imaging.BitmapCacheOption]::OnLoad
    $bitmap.StreamSource = $stream
    $bitmap.EndInit()
    $bitmap.Freeze()
    $stream.Dispose()
    $preview.Source = $bitmap
}

$window.FindName('Start').Add_Click({
    # Starting again applies changed settings to a fresh background process.
    $seconds = 0.0
    if (-not [double]::TryParse($interval.Text, [ref]$seconds) -or $seconds -lt 0.5) {
        [System.Windows.MessageBox]::Show('Enter a refresh interval of at least 0.5 seconds.') | Out-Null
        return
    }
    if (-not (Stop-Dashboard)) { return }
    $arguments = @($script, '--accent', $accent, '--text-color', $textColor, '--muted-color', $mutedColor, '--rotation', "$(Get-Rotation)", '--interval', "$seconds", '--brightness', "$([int]$brightness.Value)", '--settings-file', ('"{0}"' -f $settingsFile))
    if ($selectedImage) { $arguments += @('--background', ('"{0}"' -f $selectedImage), '--fit', (Get-Fit)) }
    # The worker must not inherit the control window's output pipes.
    $process = Start-Process -FilePath $python -ArgumentList $arguments -WorkingDirectory $appDir -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $appDir 'dashboard.log') -RedirectStandardError (Join-Path $appDir 'dashboard-error.log')
    @{ pid = $process.Id; startUtcTicks = $process.StartTime.ToUniversalTime().Ticks } |
        ConvertTo-Json | Set-Content -LiteralPath $pidFile -Encoding UTF8
    Start-Sleep -Milliseconds 800
    if (-not (Get-Process -Id $process.Id -ErrorAction SilentlyContinue)) {
        Remove-Item -LiteralPath $pidFile -ErrorAction SilentlyContinue
        $errorText = Get-Content -LiteralPath (Join-Path $appDir 'dashboard-error.log') -Tail 5 -ErrorAction SilentlyContinue | Out-String
        [System.Windows.MessageBox]::Show("The dashboard could not start. Close TT RGB Plus and try again.`n$errorText") | Out-Null
        return
    }
    Save-Settings
    Show-Preview
    $status.Text = 'Live dashboard is running.'
})

$window.FindName('Stop').Add_Click({ [void](Stop-Dashboard) })
$window.FindName('Choose').Add_Click({
    $dialog = [Microsoft.Win32.OpenFileDialog]::new()
    $dialog.Filter = 'Images|*.png;*.jpg;*.jpeg;*.bmp;*.webp|All files|*.*'
    if ($dialog.ShowDialog($window)) {
        try { $script:selectedImage = Import-Image $dialog.FileName } catch {
            [System.Windows.MessageBox]::Show("Image could not be imported: $($_.Exception.Message)", 'Tower 500 LCD') | Out-Null
            return
        }
        Refresh-Library
        Save-Settings
        Show-Preview
        $status.Text = "Background selected. Start the dashboard to show it on the LCD."
    }
})
$window.FindName('Clear').Add_Click({
    $script:selectedImage = $null
    $imageLibrary.SelectedIndex = -1
    Save-Settings
    Show-Preview
    $status.Text = 'Background cleared. Start the dashboard to apply.'
})
$window.FindName('Send').Add_Click({
    if (-not $selectedImage) {
        [System.Windows.MessageBox]::Show('Choose an image first.') | Out-Null
        return
    }
    if (-not (Stop-Dashboard)) { return }
    $result = & $python $script --image $selectedImage --rotation (Get-Rotation) --fit (Get-Fit) --brightness ([int]$brightness.Value) 2>&1
    if ($LASTEXITCODE -eq 0) { $status.Text = 'Still image sent without text; live dashboard stopped.' }
    else { [System.Windows.MessageBox]::Show(($result | Out-String)) | Out-Null }
})
function Select-Color($current) {
    Add-Type -AssemblyName System.Windows.Forms
    $dialog = [System.Windows.Forms.ColorDialog]::new()
    $dialog.Color = [System.Drawing.ColorTranslator]::FromHtml($current)
    if ($dialog.ShowDialog() -eq [System.Windows.Forms.DialogResult]::OK) {
        return ('#{0:X2}{1:X2}{2:X2}' -f $dialog.Color.R, $dialog.Color.G, $dialog.Color.B)
    }
    return $null
}

$window.FindName('AccentColor').Add_Click({
    $picked = Select-Color $accent
    if ($picked) { $script:accent = $picked; Save-Settings; Show-Preview; $status.Text = 'Accent selected. Start dashboard to apply.' }
})
$window.FindName('TextColor').Add_Click({
    $picked = Select-Color $textColor
    if ($picked) { $script:textColor = $picked; Save-Settings; Show-Preview; $status.Text = 'Main text color selected. Start dashboard to apply.' }
})
$rotation.Add_SelectionChanged({
    if ($preview) { Save-Settings; Show-Preview }
})
$fit.Add_SelectionChanged({
    if ($preview) { Save-Settings; if ($selectedImage) { Show-Preview } }
})
$imageLibrary.Add_SelectionChanged({
    if ($imageLibrary.SelectedItem) {
        $script:selectedImage = [string]$imageLibrary.SelectedItem.Tag
        Save-Settings
        Show-Preview
        $status.Text = 'Library image selected. Start the dashboard to apply.'
    }
})
$brightness.Add_ValueChanged({
    # The running worker reads the saved value on its next refresh.
    $brightnessValue.Text = '{0:0}%' -f $brightness.Value
    Save-Settings
})
$brightness.Add_PreviewMouseLeftButtonUp({ Show-Preview })

Refresh-Library
Save-Settings
Show-Preview
$status.Text = if (Get-DashboardProcess) { 'Live dashboard is running.' } else { 'Ready.' }
$watch = [System.Windows.Threading.DispatcherTimer]::new()
$watch.Interval = [TimeSpan]::FromSeconds(1)
$watch.Add_Tick({
    if ($status.Text -eq 'Live dashboard is running.' -and -not (Get-DashboardProcess)) {
        $status.Text = 'Dashboard stopped. TT RGB Plus may have taken control.'
    }
})
$watch.Start()
# Keep the clock and load preview current while the control window is open.
$previewWatch = [System.Windows.Threading.DispatcherTimer]::new()
$previewWatch.Interval = [TimeSpan]::FromSeconds(2)
$previewWatch.Add_Tick({ Show-Preview })
$previewWatch.Start()
try { $window.ShowDialog() | Out-Null } finally {
    try {
        $record = Get-Content -LiteralPath $uiPidFile -Raw | ConvertFrom-Json
        if ([int]$record.pid -eq $PID) { Remove-Item -LiteralPath $uiPidFile -ErrorAction SilentlyContinue }
    } catch { }
}
