# Tower 500 LCD

A small Windows controller for the Thermaltake Tower 500 3.9-inch LCD (USB `264A:233D`, 480 × 128 pixels). The live dashboard shows the time, CPU Load, GPU Load, and RAM Load. The clock sits near the top with about 20 pixels of space; the date is omitted. Each load reading sits on its own translucent rounded card, separated from the others. A thick, antialiased 270° circular gauge wraps the title and percentage, opens at the bottom, and fills according to the load. Load titles are white. Text has a black outline so it remains readable over a background image.

This folder is the application. Keep its files together in a permanent location. It uses the existing Windows HID driver and the open-source USB transport from [Kraken LCD Studio](https://github.com/krakenunbound/kraken-lcd-studio). It does not install a driver or write firmware.

## Requirements on this computer

- Windows and the connected Thermaltake panel with USB ID `264A:233D`.
- The Python runtime bundled with Codex at `C:\Users\User\.cache\codex-runtimes\codex-primary-runtime\dependencies\python`. Installing Python separately is unnecessary here. If Codex removes or relocates this runtime, update `$runtime` in `Open LCD App.ps1`, `Start Dashboard.ps1`, and `Start LCD at Login.ps1`, or provide a compatible Python 3.12 runtime with Pillow installed.
- The included `TowerLCDDisplay.exe`, `tower_display.py`, `thermaltake_lcd.py`, `pywinusb`, and `psutil`. Keep them together in this folder.
- NVIDIA's installed driver and `nvml.dll` for GPU Load. This computer has an NVIDIA GeForce RTX 5090 and AMD Radeon(TM) Graphics. The dashboard reads **NVIDIA GPU 0, the RTX 5090**, through NVML; it does not read the AMD GPU. Other monitor apps may report the AMD GPU or use different sampling and aggregation, so percentages can differ. If the NVIDIA driver cannot report utilization, the display shows `N/A`.
- TT RGB Plus closed while this controller uses the LCD. Both programs cannot control the panel at once.

## First run

1. Close TT RGB Plus, including its tray/background process.
2. Double-click **`Open LCD App.cmd`** in this folder. This opens the control window; it is not the background display process.
3. Click **Import image…** to copy a PNG, JPEG, BMP, or WebP into this folder's `images` library. Pick any imported image from the **Image library** dropdown. The saved selection uses the local copy, so deleting the original file will not break the dashboard. The previously selected image was also copied into this library. Live text is drawn over it on every refresh. The preview updates when you change the selection or settings and refreshes every two seconds while open; it does not access the LCD.
4. Set **Rotate** to match the physical panel direction. Rotation is clockwise: 0°, 90°, 180°, or 270°. Set **Image fit** to crop the image to fill the screen or show the whole image with bars.
5. Optionally change the accent and main value text colors, refresh interval, and **Brightness** slider. Load titles stay white. The minimum refresh interval is 0.5 seconds; the default is 2 seconds. Brightness ranges from 0% (black image) to 100% (full image). It dims the transmitted pixels, not the LCD backlight, so 0% may still have backlight glow.
6. Click **Start live dashboard**. Settings are saved in `lcd-settings.json`. Close the control window when finished; the smaller display process keeps running.

The **Stop** button ends this app's dashboard process and leaves the last frame visible. **Send still image only** sends the chosen image once, without text, and exits. Brightness changes reach a running dashboard on its next refresh, usually within two seconds. To apply other changed settings, click **Start live dashboard** again.

The controller checks for TT RGB Plus several times per second. If TT starts, the dashboard releases the panel and exits. For an immediate handoff, click **Stop** first. This app only stops its own verified LCD process. After TT closes, start the dashboard again; it does not automatically resume.

## Start automatically when you sign in

First complete **First run** so `lcd-settings.json` has the image, rotation, colors, and refresh interval you want at sign-in. Then:

1. Press **Win+R**, type `shell:startup`, and press **Enter**. Windows opens your personal Startup folder.
2. In this application's folder, right-click **`Start LCD at Login.cmd`** and choose **Show more options → Create shortcut** (or **Create shortcut**, depending on Windows). If Windows offers to put the shortcut on the desktop, accept it.
3. Move **the shortcut** into the Startup folder. Keep the `.cmd` file and the rest of this application in their current folder. The shortcut target should point to this folder's `Start LCD at Login.cmd`.
4. Double-click the shortcut once to check it. If the dashboard is already running, the launcher leaves it alone. At your next sign-in, Windows runs the same launcher automatically.

The launcher briefly runs PowerShell, reads `lcd-settings.json`, stops any older display process from this app folder, starts one hidden `TowerLCDDisplay.exe` process, and exits. Running the launcher again replaces the old process; it will not leave two dashboards fighting for the USB panel. Opening the control window again also replaces its previous window. If TT RGB Plus is already running, the launcher stops this app's old process and skips starting a new one. No Startup shortcut is installed automatically. To disable automatic startup, delete the shortcut from the Startup folder.

**Keep this folder at the same path.** If you move it, update the Startup shortcut. The included runtime path is also specific to this computer and may change after Codex updates.

## Run from PowerShell

Open PowerShell in this folder and run:

```powershell
& '.\Start Dashboard.ps1'
```

This runs in the current PowerShell window. Press **Ctrl+C** to stop. To send a still frame and exit:

```powershell
& '.\Start Dashboard.ps1' --image 'C:\path\to\picture.png'
```

To use an image behind live readings and rotate the display:

```powershell
& '.\Start Dashboard.ps1' --background 'C:\path\to\picture.png' --rotation 90 --fit cover
```

Color and timing options can be combined:

```powershell
& '.\Start Dashboard.ps1' --text-color '#FFCC66' --accent '#FF7A45' --interval 5
```

To dim the whole transmitted image to 50% from PowerShell:

```powershell
& '.\Start Dashboard.ps1' --brightness 50
```

`--fit cover` crops to fill the 480 × 128 screen; `--fit contain` shows the whole image with bars. `--rotation` accepts 0, 90, 180, or 270. `--preview C:\path\to\preview.png` saves one frame without contacting the panel.

## How it runs and what to check

`TowerLCDDisplay.exe` is a renamed copy of the Python 3.12 interpreter with its Windows file description set to **Tower 500 LCD**. It runs `tower_display.py`; this is still Python code. Task Manager should show **Tower 500 LCD** under Processes and `TowerLCDDisplay.exe` under Details. `dashboard.pid` records the current background process. The launcher finds and replaces workers by executable path, so a stale PID record cannot cause a duplicate. `control.pid` helps a newly opened control window replace the previous window.

The project has its own Git repository and `.gitignore`. Source, launchers, bundled dependencies, licenses, README, and imported images are tracked. Machine-specific `lcd-settings.json`, previews, PID records, and logs are ignored. After cloning the repository to a new computer, configure a compatible Python runtime path and choose an image again; your personal settings are not in Git.

If the screen does not update, check that TT RGB Plus is closed, the USB cable is connected, and `TowerLCDDisplay.exe` appears in Task Manager. Open the control window and click **Start live dashboard**. If startup fails, see `dashboard-error.log` and `dashboard.log` in this folder. If the window fails to open, run `Open LCD App.ps1` from PowerShell to see its error. The panel can keep its last frame when no process is running; that alone does not mean the dashboard is active.

On this computer, the earlier CPU/RAM dashboard used about **28 MB RAM**. With NVIDIA GPU monitoring enabled, the current background process measured about **46 MB RAM**. Rendering/JPEG encoding previously took about **5 ms CPU time per frame** at 480 × 128; GPU monitoring and USB transfer add overhead. The PowerShell control window used about **173 MB RAM while open**; closing it leaves only the smaller display process. Actual use varies with image and settings.

## Files and licensing

- `Open LCD App.cmd` / `.ps1`: control window; saves settings and starts or stops the background process.
- `Start LCD at Login.cmd` / `.ps1`: hidden, one-shot sign-in launcher using saved settings.
- `Start Dashboard.ps1`: direct command-line launcher.
- `tower_display.py`: draws frames and runs the refresh loop.
- `thermaltake_lcd.py`: USB transport copied from Kraken LCD Studio.
- `TowerLCDDisplay.exe`: renamed Python interpreter; the Python library comes from the Codex runtime path above.

`thermaltake_lcd.py` is GPL-3.0-or-later; see `LICENSE-Kraken.txt`. `tower_display.py` is GPL-3.0-or-later so it can be shared with that transport. Bundled `pywinusb` and `psutil` are BSD-3-Clause; see their included notices. The Python license is in `LICENSE-Python.txt`.

For video, GIFs, profiles, and other advanced controls, see [Kraken LCD Studio](https://github.com/krakenunbound/kraken-lcd-studio).
