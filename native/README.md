# Thermaltake500LCD — native C++ controller

The C++ replacement for the original Python/PowerShell Tower 500 LCD controller.
Supports the Thermaltake 3.9-inch 480×128 USB HID panel, VID 264A / PID 233D.
Both applications can be installed side-by-side, but only one LCD worker can
control the physical panel at a time.

## Executables

- thermaltake500lcd.exe — Windows Win32 control window: live preview, Start/Stop,
  image import/library, still image, rotation, image fit, refresh interval, brightness,
  accent and text colours.
- thermaltake500lcd.service.exe — detached background worker. Started by the
  control window or a shortcut at Windows sign-in. It owns the USB HID connection,
  polls CPU/RAM/NVIDIA GPU 0, draws clock + three circular gauges, JPEG-encodes
  frames, and transmits them to the display.

IMPORTANT: the second executable is a per-user background worker, not an
SCM/Session 0 Windows Service. This intentionally retains access to the user's
LocalAppData, pictures, and USB device without administrative service setup.
The worker stays running after the control window closes.

## Installation

1. On GitHub, go to Actions → Native C++ Windows build, open the latest
   successful run and download Thermaltake500LCD-Windows-x64.
2. Extract all files into a temporary directory.
3. Close TT RGB Plus. Run Install.cmd. By default the app goes under
   C:\Program Files\Thermaltake500LCD. Windows prompts for UAC.
4. Run thermaltake500lcd.exe from the install directory. Pick an image, then
   press Start live dashboard.
5. The installer adds a per-user Windows Startup shortcut for the new worker;
   add -NoStartup when invoking Install.ps1 to disable this.

The installer stops the previous standalone LCD worker, disables old
Start LCD at Login.cmd shortcuts by renaming them to .lnk.disabled, and
does not delete Python source, files, or saved images. Remove the new
Startup shortcut to disable automatic start later.

Settings are backwards compatible with the Python app's
%LOCALAPPDATA%\Tower500LCD\lcd-settings.json and
%LOCALAPPDATA%\Tower500LCD\images. The old JSON file is reused. Runtime
diagnostics are written to
%LOCALAPPDATA%\Tower500LCD\native-dashboard.log.

The worker coordinates through per-user named Windows mutex + stop event
rather than killing unrelated Python processes. If USB I/O fails, it closes
both HID interfaces and attempts to reconnect after three seconds. Starting
TT RGB Plus makes it release the panel and exit. It will not restart until
you start the worker again or sign in.

## Build from source (Windows x64)

Requires Visual Studio 2022 Build Tools (C++ desktop workload) and CMake.
No Python, PyInstaller, or third-party C++ SDK is needed to run either
executable. Windows SDK libraries (HID, SetupAPI, GDI+) are used at build time.

    cmake -S native -B native/build -A x64
    cmake --build native/build --config Release

Outputs:

    native/build/Release/thermaltake500lcd.exe
    native/build/Release/thermaltake500lcd.service.exe

To install from a local build, put the two executables next to
native/Install.ps1 and native/Install.cmd.

## Compatibility and limitations

- LCD protocol and handshake match the original 264A:233D controller; other
  Thermaltake devices are not assumed compatible.
- Windows GDI+ supports PNG, JPEG, BMP, and GIF. WebP may need to be converted
  to PNG if a compatible decoder is unavailable.
- GPU utilization uses NVIDIA NVML GPU index 0. If NVML fails, GPU shows N/A.
- Brightness is software pixel dimming, not backlight power control.
- This cannot guarantee hardware flickering is fixed: a faulty USB cable/port
  or competing LCD application can produce similar symptoms.
- The legacy Python build remains in the repository as a fallback.
- Keep both native executables in the same directory.

## Troubleshooting

If numbers disappear or the LCD flickers:
1. Exit TT RGB Plus and any other controller using the LCD.
2. Stop the old Tower500LcdProcess.exe if it is still running.
3. Open the native control window, press Stop, then Start.
4. Inspect native-dashboard.log for repeated HID timeouts/reconnections.
5. If it still flashes purple, test USB power/cabling and the vendor app.

Licensing: newly authored native C++ files are distributed under
GPL-3.0-or-later, compatible with the project's existing GPL components.
