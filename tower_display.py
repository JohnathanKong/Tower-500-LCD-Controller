"""Small live display for the Thermaltake 480x128 USB panel (264A:233D).

Uses Kraken LCD Studio's GPL-3.0-or-later HID transport, included beside this file.
"""

from __future__ import annotations

import argparse
import ctypes
import io
import json
import os
import sys
import time
from datetime import datetime
from pathlib import Path

import psutil
from PIL import Image, ImageColor, ImageDraw, ImageFont, ImageOps

from thermaltake_lcd import HEIGHT, WIDTH, LcdError, ThermaltakeLcd


class GpuMonitor:
    """Read NVIDIA GPU utilization directly from the installed NVML driver."""

    class Utilization(ctypes.Structure):
        _fields_ = [("gpu", ctypes.c_uint), ("memory", ctypes.c_uint)]

    def __init__(self) -> None:
        self.library = None
        self.handle = ctypes.c_void_p()
        try:
            library = ctypes.WinDLL("nvml.dll")
            library.nvmlInit_v2.restype = ctypes.c_int
            library.nvmlDeviceGetHandleByIndex_v2.argtypes = [ctypes.c_uint, ctypes.POINTER(ctypes.c_void_p)]
            library.nvmlDeviceGetHandleByIndex_v2.restype = ctypes.c_int
            library.nvmlDeviceGetUtilizationRates.argtypes = [ctypes.c_void_p, ctypes.POINTER(self.Utilization)]
            library.nvmlDeviceGetUtilizationRates.restype = ctypes.c_int
            if library.nvmlInit_v2() != 0 or library.nvmlDeviceGetHandleByIndex_v2(0, ctypes.byref(self.handle)) != 0:
                return
            self.library = library
        except (AttributeError, OSError):
            pass

    def usage(self) -> float | None:
        """Return GPU load in percent, or None if the driver cannot report it."""
        if self.library is None:
            return None
        rates = self.Utilization()
        if self.library.nvmlDeviceGetUtilizationRates(self.handle, ctypes.byref(rates)) != 0:
            return None
        return float(rates.gpu)


def thermaltake_running() -> bool:
    """Detect the vendor app without affecting unrelated Python processes."""
    for process in psutil.process_iter(["name"]):
        try:
            name = (process.info["name"] or "").lower().removesuffix(".exe")
            if name.startswith("ttrgbplus") or name.startswith("tt rgb plus"):
                return True
        except (psutil.NoSuchProcess, psutil.AccessDenied):
            continue
    return False


def replace_older_instances() -> None:
    """A newly launched display owns the panel; stop older copies of this app."""
    executable = os.path.normcase(os.path.abspath(sys.executable))
    for process in psutil.process_iter(["pid", "exe"]):
        try:
            if process.pid == os.getpid():
                continue
            other = process.info["exe"]
            if other and os.path.normcase(os.path.abspath(other)) == executable:
                process.kill()
                process.wait(timeout=3)
        except (psutil.NoSuchProcess, psutil.AccessDenied, psutil.TimeoutExpired):
            continue


def font(size: int, bold: bool = False):
    name = "segoeuib.ttf" if bold else "segoeui.ttf"
    try:
        return ImageFont.truetype(f"C:/Windows/Fonts/{name}", size)
    except OSError:
        return ImageFont.load_default()


def _logical_size(rotation: int) -> tuple[int, int]:
    """Lay out text upright first, then rotate the complete frame for the panel."""
    return (HEIGHT, WIDTH) if rotation in (90, 270) else (WIDTH, HEIGHT)


def _orient(image: Image.Image, rotation: int) -> Image.Image:
    return image.rotate(-rotation, expand=True) if rotation else image


def _centered_text(draw: ImageDraw.ImageDraw, text: str, y: int, face: ImageFont.ImageFont, color: str, left: int, right: int) -> None:
    """Center outlined text inside a horizontal region, including its font bearing."""
    bounds = draw.textbbox((0, 0), text, font=face, stroke_width=2)
    width = bounds[2] - bounds[0]
    x = round((left + right - width) / 2 - bounds[0])
    draw.text((x, y), text, font=face, fill=color, stroke_width=2, stroke_fill="black")


def dashboard(accent: str, text_color: str = "#F2F7FA", muted_color: str = "#95ADBE", rotation: int = 0, background: Image.Image | None = None, gpu_usage: float | None = None, cpu_usage: float | None = None) -> Image.Image:
    """Render one complete frame; the USB loop calls this for every refresh."""
    now = datetime.now()
    cpu = psutil.cpu_percent(interval=None) if cpu_usage is None else cpu_usage
    memory = psutil.virtual_memory().percent
    readings = (("CPU LOAD", cpu), ("GPU LOAD", gpu_usage), ("RAM LOAD", memory))
    w, h = _logical_size(rotation)
    image = background.copy() if background is not None else Image.new("RGB", (w, h), "#071019")
    if image.size != (w, h):
        raise ValueError(f"Background must be {w}x{h} before rotation")
    portrait = rotation in (90, 270)
    d = ImageDraw.Draw(image)
    if background is None:
        d.rounded_rectangle((2, 2, w - 3, h - 3), radius=15, outline="#254356", width=2)
    if portrait:
        # The clock and all three values share one font and the full panel width.
        value_font = font(34, True)
        _centered_text(d, now.strftime("%I:%M").lstrip("0"), 20, value_font, text_color, 8, w - 8)
        for y, (label, value) in zip((92, 214, 336), readings):
            _centered_text(d, label, y, font(16, True), "#FFFFFF", 8, w - 8)
            _centered_text(d, f"{value:0.0f}%" if value is not None else "N/A", y + 27, value_font, text_color, 8, w - 8)
            d.rounded_rectangle((17, y + 79, w - 18, y + 87), radius=4, fill="#234052")
            if value is None:
                continue
            end = 17 + round((w - 35) * min(max(value, 0), 100) / 100)
            if end > 17:
                d.rounded_rectangle((17, y + 79, end, y + 87), radius=4, fill=accent)
    else:
        # Four columns fit the clock and three equally sized load readings.
        value_font = font(34, True)
        columns = ((5, 115), (125, 235), (245, 355), (365, 475))
        for divider in (120, 240, 360):
            d.line((divider, 14, divider, 112), fill="#254356", width=2)
        _centered_text(d, now.strftime("%I:%M").lstrip("0"), 20, value_font, text_color, *columns[0])
        for (left, right), (label, value) in zip(columns[1:], readings):
            _centered_text(d, label, 17, font(13, True), "#FFFFFF", left, right)
            _centered_text(d, f"{value:0.0f}%" if value is not None else "N/A", 48, value_font, text_color, left, right)
            d.rounded_rectangle((left + 8, 104, right - 8, 111), radius=3, fill="#234052")
            if value is None:
                continue
            end = left + 8 + round((right - left - 16) * min(max(value, 0), 100) / 100)
            if end > left + 8:
                d.rounded_rectangle((left + 8, 104, end, 111), radius=3, fill=accent)
    return _orient(image, rotation)


def load_background(path: Path, rotation: int = 0, fit: str = "cover") -> Image.Image:
    """Resize once at startup, in the upright layout, instead of every frame."""
    with Image.open(path) as source:
        source = source.convert("RGB")
        size = _logical_size(rotation)
        if fit == "contain":
            image = Image.new("RGB", size, "#071019")
            scaled = ImageOps.contain(source, size, method=Image.Resampling.LANCZOS)
            image.paste(scaled, ((size[0] - scaled.width) // 2, (size[1] - scaled.height) // 2))
        else:
            image = ImageOps.fit(source, size, method=Image.Resampling.LANCZOS)
        return image


def image_frame(path: Path, rotation: int = 0, fit: str = "cover") -> Image.Image:
    return _orient(load_background(path, rotation, fit), rotation)


def jpeg_bytes(image: Image.Image) -> bytes:
    """The panel transport accepts a JPEG payload for each USB update."""
    stream = io.BytesIO()
    image.save(stream, format="JPEG", quality=88, optimize=True, progressive=False, subsampling=0)
    return stream.getvalue()


def dim_frame(image: Image.Image, brightness: int) -> Image.Image:
    """Darken every pixel; this does not change the LCD's backlight power."""
    if brightness >= 100:
        return image
    return Image.blend(Image.new("RGB", image.size, "black"), image.convert("RGB"), brightness / 100)


def saved_brightness(path: Path | None, fallback: int) -> int:
    """Read live brightness changes made by the control window, if available."""
    if path is None:
        return fallback
    try:
        value = json.loads(path.read_text(encoding="utf-8-sig")).get("brightness")
        if isinstance(value, (int, float)) and 0 <= value <= 100:
            return round(value)
    except (OSError, ValueError, AttributeError):
        pass
    return fallback


def main() -> int:
    parser = argparse.ArgumentParser(description="Thermaltake Tower 500 LCD controller")
    media = parser.add_mutually_exclusive_group()
    media.add_argument("--image", type=Path, help="send one image without dashboard text")
    media.add_argument("--background", type=Path, help="use image behind the live dashboard")
    parser.add_argument("--fit", choices=("cover", "contain"), default="cover", help="crop to fill or show the whole image")
    parser.add_argument("--interval", type=float, default=2.0, help="dashboard refresh seconds")
    parser.add_argument("--accent", default="#28DEBB", help="dashboard accent color")
    parser.add_argument("--text-color", default="#F2F7FA", help="clock and value text color")
    parser.add_argument("--muted-color", default="#95ADBE", help="reserved for older saved settings")
    parser.add_argument("--rotation", type=int, choices=(0, 90, 180, 270), default=0, help="clockwise panel rotation")
    parser.add_argument("--brightness", type=int, default=100, help="software image brightness from 0 (black) to 100 (full)")
    parser.add_argument("--settings-file", type=Path, help="read brightness changes from the control window while running")
    parser.add_argument("--preview", type=Path, help="save a preview PNG and do not contact the LCD")
    args = parser.parse_args()
    if args.interval < 0.5:
        parser.error("--interval must be at least 0.5 seconds")
    if not 0 <= args.brightness <= 100:
        parser.error("--brightness must be between 0 and 100")
    media_path = args.image or args.background
    if media_path and not media_path.is_file():
        parser.error(f"image not found: {media_path}")
    for name in ("accent", "text_color", "muted_color"):
        try:
            ImageColor.getrgb(getattr(args, name))
        except ValueError:
            parser.error(f"--{name.replace('_', '-')} must be a valid color")
    try:
        gpu_monitor = GpuMonitor()
        background = load_background(args.background, args.rotation, args.fit) if args.background else None
        # A fresh preview process needs a brief CPU sample before drawing.
        preview_cpu = psutil.cpu_percent(interval=0.1) if args.preview and not args.image else None
        frame = image_frame(args.image, args.rotation, args.fit) if args.image else dashboard(args.accent, args.text_color, args.muted_color, args.rotation, background, gpu_monitor.usage(), preview_cpu)
        current_brightness = saved_brightness(args.settings_file, args.brightness)
        if args.preview:
            dim_frame(frame, current_brightness).save(args.preview)
            print(f"Preview saved: {args.preview}")
            return 0
        if thermaltake_running():
            raise LcdError("TT RGB Plus is running; close it before using this LCD controller")
        replace_older_instances()
        with ThermaltakeLcd() as lcd:
            if args.image:
                # A still image is sent once; the panel retains that frame after exit.
                lcd.send_jpeg(jpeg_bytes(dim_frame(frame, current_brightness)))
                print("Image sent. It will remain until another app changes the panel.")
            else:
                # Prime psutil's CPU counter before the first measured frame.
                psutil.cpu_percent(interval=None)
                print("Dashboard running. Press Ctrl+C to stop.")
                while True:
                    # Give the vendor software the USB panel whenever it starts.
                    if thermaltake_running():
                        print("TT RGB Plus started; releasing LCD and exiting.")
                        return 0
                    current_brightness = saved_brightness(args.settings_file, current_brightness)
                    rendered = dashboard(args.accent, args.text_color, args.muted_color, args.rotation, background, gpu_monitor.usage())
                    lcd.send_jpeg(jpeg_bytes(dim_frame(rendered, current_brightness)), log_frame=False)
                    deadline = time.monotonic() + args.interval
                    while time.monotonic() < deadline:
                        if thermaltake_running():
                            print("TT RGB Plus started; releasing LCD and exiting.")
                            return 0
                        time.sleep(min(0.25, max(0, deadline - time.monotonic())))
    except KeyboardInterrupt:
        print("Stopped.")
    except (LcdError, OSError, ValueError) as exc:
        print(f"LCD error: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
