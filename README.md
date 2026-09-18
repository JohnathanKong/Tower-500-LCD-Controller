# Tower 500 LCD

A small Windows controller for the Thermaltake Tower 500 3.9-inch LCD (USB `264A:233D`, 480 × 128 pixels). The live dashboard shows the time and CPU, GPU, and RAM utilization. The clock sits near the top with about 20 pixels of space; the date is omitted. Each reading sits on its own translucent rounded card, separated from the others. A thick, antialiased 270° circular gauge with rounded ends wraps the large CPU, GPU, or RAM title and percentage, opens at the bottom, and fills according to utilization. Titles are white. Text has a black outline so it remains readable over a background image.

The runnable app is copied to your chosen location by `Install Tower500Lcd.cmd`. Keep its files together, especially `bundle\Tower500LcdProcess.exe` and the entire `bundle\_internal` directory. The bundled executable includes Python and the libraries it needs; **running the app does not require Python or Codex to be installed**. It uses the existing Windows HID driver and the open-source USB transport from [Kraken LCD Studio](https://github.com/krakenunbound/kraken-lcd-studio). It does not install a driver or write firmware.

## Requirements on this computer

- Windows and the connected Thermaltake panel with USB ID `264A:233D`.
- The included `bundle\Tower500LcdProcess.exe` and `bundle\_internal` directory. They contain the Python runtime, Pillow, psutil, pywinusb, and the display code. Keep the complete bundle together. Windows PowerShell, used for the control window and launchers, is built into Windows.
- NVIDIA's installed driver and `nvml.dll` for GPU Load. This computer has an NVIDIA GeForce RTX 5090 and AMD Radeon(TM) Graphics. The dashboard reads **NVIDIA GPU 0, the RTX 5090**, through NVML; it does not read the AMD GPU. Other monitor apps may report the AMD GPU or use different sampling and aggregation, so percentages can differ. If the NVIDIA driver cannot report utilization, the display shows `N/A`.
- TT RGB Plus closed while this controller uses the LCD. Both programs cannot control the panel at once.

## Install or update

From the Git project folder, double-click **`Install Tower500Lcd.cmd`**. It copies the runnable files directly to `C:\Program Files\Tower500Lcd`; Windows will ask for administrator approval. No `install` folder is created in the project.

To choose another destination, run the PowerShell script with a path. Relative paths are resolved from the Git project folder:

```powershell
& '.\Install Tower500Lcd.ps1' -Destination 'D:\Apps\Tower500Lcd'
```

The installer copies only the standalone bundle, launchers, README, and license notices (about 30 MB). Running it again updates those files at the same destination and removes the old `Open LCD App` launcher names. If a display process is running from that destination, installation stops it so the executable can be replaced; launch `Tower500Lcd.cmd` or the sign-in shortcut afterward. The installer leaves unrelated files in an existing destination alone. It does not copy source code, build tools, logs, settings, or images. Your existing image library and settings remain in `%LOCALAPPDATA%\Tower500LCD`; on another computer, import an image in the control window.

## First run

1. Close TT RGB Plus, including its tray/background process.
2. Double-click **`Tower500Lcd.cmd`** in this folder. This opens the control window; it is not the background display process.
3. Click **Import image…** to copy a PNG, JPEG, BMP, or WebP into `%LOCALAPPDATA%\Tower500LCD\images`. Pick any imported image from the **Image library** dropdown. The saved selection uses the local copy, so deleting the original file will not break the dashboard. Live text is drawn over it on every refresh. The preview updates when you change the selection or settings and refreshes every two seconds while open; it does not access the LCD.
4. Set **Rotate** to match the physical panel direction. Rotation is clockwise: 0°, 90°, 180°, or 270°. Set **Image fit** to crop the image to fill the screen or show the whole image with bars.
5. Optionally change the accent and main value text colors, refresh interval, and **Brightness** slider. Load titles stay white. The minimum refresh interval is 0.5 seconds; the default is 2 seconds. Brightness ranges from 0% (black image) to 100% (full image). It dims the transmitted pixels, not the LCD backlight, so 0% may still have backlight glow.
6. Click **Start live dashboard**. Settings are saved in `%LOCALAPPDATA%\Tower500LCD\lcd-settings.json`. Close the control window when finished; the smaller display process keeps running.

The **Stop** button ends this app's dashboard process and leaves the last frame visible. **Send still image only** sends the chosen image once, without text, and exits. Brightness changes reach a running dashboard on its next refresh, usually within two seconds. To apply other changed settings, click **Start live dashboard** again.

The controller checks for TT RGB Plus several times per second. If TT starts, the dashboard releases the panel and exits. For an immediate handoff, click **Stop** first. This app only stops its own verified LCD process. After TT closes, start the dashboard again; it does not automatically resume.

## Start automatically when you sign in

First complete **First run** so the saved settings have the image, rotation, colors, and refresh interval you want at sign-in. Then:

1. Press **Win+R**, type `shell:startup`, and press **Enter**. Windows opens your personal Startup folder.
2. In this application's folder, right-click **`Start LCD at Login.cmd`** and choose **Show more options → Create shortcut** (or **Create shortcut**, depending on Windows). If Windows offers to put the shortcut on the desktop, accept it.
3. Move **the shortcut** into the Startup folder. Keep the `.cmd` file and the rest of this application in their current folder. The shortcut target should point to this folder's `Start LCD at Login.cmd`.
4. Double-click the shortcut once to check it. If the dashboard is already running, the launcher replaces it. At your next sign-in, Windows runs the same launcher automatically.

The launcher briefly runs PowerShell, reads `lcd-settings.json`, stops any older display process from this app folder, starts one hidden `Tower500LcdProcess.exe` process, and exits. Running the launcher again replaces the old process; it will not leave two dashboards fighting for the USB panel. Opening the control window again also replaces its previous window. If TT RGB Plus is already running, the launcher stops this app's old process and skips starting a new one. No Startup shortcut is installed automatically. To disable automatic startup, delete the shortcut from the Startup folder.

**After installing:** launch `Tower500Lcd.cmd` from the destination. If your Startup shortcut points to an older location, replace it with a shortcut to the installed `Start LCD at Login.cmd`. Settings, imported images, previews, PID records, and logs stay in `%LOCALAPPDATA%\Tower500LCD`, so normal use does not need administrator rights. No Codex path or separate Python install is needed.

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

`Tower500Lcd.cmd` opens the main control window titled **Tower500Lcd**. `bundle\Tower500LcdProcess.exe` is the background display program containing the Python interpreter and libraries; Task Manager should show **Tower500LcdProcess** under Processes and `Tower500LcdProcess.exe` under Details. The display logic is still written in Python. `%LOCALAPPDATA%\Tower500LCD\dashboard.pid` records the current background process. The launcher replaces workers under either the current or previous executable name, including a copy running from an older install path, so a stale PID record cannot cause a duplicate. `control.pid` helps a newly opened control window replace the previous window.

The project has its own Git repository and `.gitignore`. Source, launchers, the standalone bundle, licenses, README, and initial images in the app folder are tracked. Personal settings and later imported images live under `%LOCALAPPDATA%\Tower500LCD` and are not in Git. After cloning the repository to another Windows computer, no Python setup is needed to run the included build; choose an image again.

If the screen does not update, check that TT RGB Plus is closed, the USB cable is connected, and `Tower500LcdProcess.exe` appears in Task Manager. Open the control window and click **Start live dashboard**. If startup fails, see `dashboard-error.log` and `dashboard.log` in `%LOCALAPPDATA%\Tower500LCD`. If the window fails to open, run `Tower500Lcd.ps1` from PowerShell to see its error. The panel can keep its last frame when no process is running; that alone does not mean the dashboard is active.

On this computer, the standalone background process measured about **46 MB working memory** and **34 MB private memory** during the live test at a two-second refresh interval. The PowerShell control window used about **173 MB RAM while open** in an earlier test; closing it leaves only the smaller display process. Actual use varies with image and settings. The standalone files use about **30 MB on disk**.

## Which files to run

| File | What it does |
| --- | --- |
| `Install Tower500Lcd.cmd` | **Run from the Git project** to install or update the app in Program Files. Its `.ps1` partner accepts a custom `-Destination`. Neither file is copied into the installed app. |
| `Tower500Lcd.cmd` | **Double-click this** to open the control window. The window can close while the display keeps running. |
| `Start LCD at Login.cmd` | Make a shortcut to this in the Windows Startup folder. It starts one hidden display process using saved settings, then exits. |
| `Start Dashboard.ps1` | Optional manual command-line launcher. It stays in your PowerShell window until you stop it. |
| `bundle\Tower500LcdProcess.exe` | The actual display program. The launchers call it; you normally do not open it directly. |
| `Tower500Lcd.ps1`, `Start LCD at Login.ps1` | PowerShell code behind the two `.cmd` launchers. Keep these beside the `.cmd` files. |
| `AppState.ps1` | Helper that keeps changing files in `%LOCALAPPDATA%\Tower500LCD`. Keep it with the launchers; do not run it directly. |
| `bundle\_internal` | Private runtime and libraries required by the executable. Keep the complete directory; do not run files in it directly. |

The Git project also contains `tower_display.py` (dashboard logic), `thermaltake_lcd.py` (USB transport), and bundled dependency source. These are **source files**, not copied to the installed app. `Install Tower500Lcd.cmd` / `.ps1` copy the runnable app; `Build Standalone.ps1`, `requirements-build.txt`, `hooks`, and `version-info.txt` are for rebuilding the executable after source changes. Do not put the build tools in Program Files.

## Licensing

`thermaltake_lcd.py` is GPL-3.0-or-later; see `LICENSE-Kraken.txt`. `tower_display.py` is GPL-3.0-or-later so it can be shared with that transport. Bundled `pywinusb` and `psutil` are BSD-3-Clause; see their included notices. Python, Pillow, and PyInstaller license texts are in `LICENSE-Python.txt`, `LICENSE-Pillow.txt`, and `LICENSE-PyInstaller.txt`.

## Rebuild from source

Running the app needs no build tools. To rebuild the bundled executable after changing Python code, use **Python 3.12 on Windows**. From this folder, install the pinned build tools and run the build script:

```powershell
python -m pip install --no-deps --target .build-deps -r requirements-build.txt
& '.\Build Standalone.ps1' -PythonExe 'C:\path\to\python.exe'
```

The script creates `.build-dist\Tower500LcdProcess`. Stop the running display before replacing `bundle\Tower500LcdProcess.exe` and `bundle\_internal` with the newly built pair. Test a preview with `& '.\Start Dashboard.ps1' --preview '.\preview.png'`, then start the live dashboard. The build tools and intermediate output are ignored by Git; the completed `bundle` is tracked.

For video, GIFs, profiles, and other advanced controls, see [Kraken LCD Studio](https://github.com/krakenunbound/kraken-lcd-studio).
