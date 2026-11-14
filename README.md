# Thermaltake500LCD

Native Windows C++ controller for the Thermaltake Tower 500 3.9-inch USB LCD (480 × 128, USB VID:PID `264A:233D`). **The main branch contains only the C++ implementation.**

## App icons

The GUI and background worker each include a native Windows icon. The shared neon LCD monitor
design is stored as editable vector artwork in `assets/app.svg` and `assets/service.svg`,
and the corresponding multiresolution `assets/*.ico` resources are embedded directly
into both Windows executables at link time. The background worker's icon adds a
small electric-bolt badge to distinguish it from the GUI.

When the SVG artwork is updated, GitHub Actions regenerates the ICO files.
Docker cross-build and MSVC build both use the same checked-in ICO resources,
so neither requires Python or an image converter on your computer.

## Applications

| Executable | Purpose |
| --- | --- |
| `thermaltake500lcd.exe` | Windows GUI for live preview, backgrounds, colours, refresh rate, brightness, rotation, image fit, and starting/stopping the display |
| `thermaltake500lcd.service.exe` | Background worker handling CPU/RAM/NVIDIA GPU monitoring, JPEG rendering, and USB HID communication |

The worker is a **per-user background process**, *not* an SCM-managed Windows Service. It continues when the GUI closes and can launch automatically at Windows sign-in.

## Install (no Python required)

1. Open [Actions → Native C++ Windows build](../../actions/workflows/native-windows.yml) and select the most recent **successful** run.
2. Download and extract **Thermaltake500LCD-Windows-x64** from the run's Artifacts section.
3. Close TT RGB Plus and the previous LCD controller.
4. Double-click **`Install.cmd`** in the extracted folder. This installs to `C:\Program Files\Thermaltake500LCD` by default and creates a Windows Startup shortcut for the C++ worker.
5. Launch `thermaltake500lcd.exe` from the installation folder. Select an image if desired and press **Start live dashboard**.

The installer stops recognized older LCD workers, disables old Python LCD Startup shortcuts, and **does not delete your existing saved pictures or settings**.

To install to another location or skip the Startup shortcut, use:

```powershell
& '.\Install.ps1' -Destination 'D:\Apps\Thermaltake500LCD' -NoStartup
```

## Build from Docker (recommended for portable builds)

You can compile both Windows .exe files **without Visual Studio or MinGW installed on your host**. Docker Desktop can run in the normal **Linux containers** mode. The image contains the MinGW-w64 cross-compiler and CMake.

From the repository root, run:

```powershell
docker compose run --build --rm build
```

After the build, the Windows installation package appears directly under `dist/`:

```text
dist/
├── thermaltake500lcd.exe
├── thermaltake500lcd.service.exe
├── Install.cmd
├── Install.ps1
├── README.md
└── LICENSE
```

The two executables can be run **portably** by keeping them together; no installer is required. `Install.cmd` is optional and configures the Program Files location and startup shortcut.

The cross-compiled executables target Windows x64, not Linux. The Docker container is for **building only** — it cannot interact with the USB LCD. This build uses MinGW-w64 instead of MSVC; GitHub Actions also runs the original Visual Studio build for comparison. Only the Windows computer with the LCD can verify hardware behaviour.

See the Docker cross-build CI status in [Actions](../../actions/workflows/docker-windows.yml).

## Build from source

Use Windows with Visual Studio 2022 C++ Desktop Build Tools, the Windows SDK, and CMake 3.22+:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
```

The executables are placed under `build\Release\`. Copy both beside `Install.ps1`, `Install.cmd`, and `README.md` to install your local build. The GitHub Actions workflow assembles this package automatically.

**No Python, PyInstaller, or third-party Python runtime is needed**. Build dependencies are provided by the Visual C++ toolchain and Windows SDK; GPU monitoring uses the installed NVIDIA NVML driver if present.

## Application data

The native app continues using the previous controller's user settings and background image library:

- Settings: `%LOCALAPPDATA%\Tower500LCD\lcd-settings.json`
- Imported images: `%LOCALAPPDATA%\Tower500LCD\images\`
- Native diagnostics: `%LOCALAPPDATA%\Tower500LCD\native-dashboard.log`

Existing settings and image files are **not** part of this Git repository.

The panel receives native JPEG frames through Windows HID drivers. The C++ worker automatically retries USB errors and exits when TT RGB Plus takes control. Brightness dims the pixels, not the display backlight. NVIDIA GPU index 0 is used for GPU load; unsupported or unavailable readings display N/A.

If your screen keeps turning purple or flickering, close other LCD software and inspect the native log for reconnect errors; a competing process, USB port/cable or the panel itself can also cause these symptoms.

## Source layout

```text
CMakeLists.txt
assets/app.svg
assets/app.ico
assets/service.svg
assets/service.ico
resources/app.rc.in
resources/service.rc.in
include/lcd.hpp
src/core.cpp
src/gui.cpp
src/service.cpp
Install.cmd
Install.ps1
README.md
LICENSE
.github/workflows/native-windows.yml
```

## Historical Python controller

The complete original Python/PowerShell release is preserved as the annotated Git tag [`python-legacy-v1`](../../tree/python-legacy-v1), pointing to commit `ac803a0cf1f45ba6599111cfb562a0433514b955`. To recover or branch from it:

```bash
git fetch origin tag python-legacy-v1
git switch -c python-legacy python-legacy-v1
```

The original Python scripts, binary bundle, and vendored Python dependencies were intentionally removed from `master`.

## License

GPL-3.0-or-later. See [LICENSE](LICENSE).
