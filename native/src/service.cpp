#include "lcd.hpp"

#include <shellapi.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <exception>
#include <stdexcept>
#include <string>
#include <thread>

using namespace lcd;

namespace {
bool sleepOrStop(HANDLE stop, DWORD milliseconds) {
    DWORD elapsed = 0;
    while (elapsed < milliseconds) {
        DWORD step = std::min<DWORD>(250, milliseconds - elapsed);
        if (WaitForSingleObject(stop, step) == WAIT_OBJECT_0) return true;
        if (vendorRunning()) return true;
        elapsed += step;
    }
    return false;
}
std::unique_ptr<Gdiplus::Bitmap> stillImage(const Settings& s) {
    auto path = resolveImage(s);
    if (path.empty()) throw std::runtime_error("Choose an image in the control window first");
    const bool portrait = s.rotation == 90 || s.rotation == 270;
    const int width = portrait ? kHeight : kWidth, height = portrait ? kWidth : kHeight;
    auto output = std::make_unique<Gdiplus::Bitmap>(width, height, PixelFormat32bppARGB);
    Gdiplus::Bitmap image(path.c_str());
    if (image.GetLastStatus() != Gdiplus::Ok)
        throw std::runtime_error("Selected image is not supported by Windows GDI+");
    Gdiplus::Graphics graphics(output.get());
    graphics.Clear(Gdiplus::Color(255, 7, 16, 25));
    float sw = static_cast<float>(image.GetWidth()), sh = static_cast<float>(image.GetHeight());
    if (sw <= 0 || sh <= 0) throw std::runtime_error("Selected image has invalid dimensions");
    float scale = s.contain ? std::min(width / sw, height / sh) : std::max(width / sw, height / sh);
    graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    graphics.DrawImage(&image, Gdiplus::RectF((width - sw * scale) / 2, (height - sh * scale) / 2, sw * scale, sh * scale));
    graphics.Flush();
    if (s.rotation == 90) output->RotateFlip(Gdiplus::Rotate90FlipNone);
    if (s.rotation == 180) output->RotateFlip(Gdiplus::Rotate180FlipNone);
    if (s.rotation == 270) output->RotateFlip(Gdiplus::Rotate270FlipNone);
    if (s.brightness < 100) {
        Gdiplus::Bitmap dimmed(kWidth, kHeight, PixelFormat32bppARGB);
        Gdiplus::Graphics g(&dimmed);
        Gdiplus::ColorMatrix matrix{};
        const float factor = std::clamp(s.brightness, 0, 100) / 100.f;
        matrix.m[0][0] = factor; matrix.m[1][1] = factor; matrix.m[2][2] = factor;
        matrix.m[3][3] = 1; matrix.m[4][4] = 1;
        Gdiplus::ImageAttributes attributes;
        attributes.SetColorMatrix(&matrix);
        g.DrawImage(output.get(), Gdiplus::Rect(0, 0, kWidth, kHeight), 0, 0, kWidth, kHeight,
                    Gdiplus::UnitPixel, &attributes);
        g.Flush();
        output = std::make_unique<Gdiplus::Bitmap>(kWidth, kHeight, PixelFormat32bppARGB);
        Gdiplus::Graphics copy(output.get());
        copy.DrawImage(&dimmed, 0, 0);
        copy.Flush();
    }
    return output;
}
int run(bool oneShot) {
    GdiplusSession graphics;
    HANDLE mutex = CreateMutexW(nullptr, FALSE, L"Local\\Thermaltake500LCD.Worker");
    if (!mutex) throw std::runtime_error("Cannot create LCD worker mutex");
    // Ask any older worker to stop, then acquire exclusive ownership of the panel.
    stopWorker();
    DWORD lock = WaitForSingleObject(mutex, 10000);
    if (lock != WAIT_OBJECT_0 && lock != WAIT_ABANDONED) {
        CloseHandle(mutex);
        throw std::runtime_error("Another LCD worker did not release the panel");
    }
    HANDLE stop = CreateEventW(nullptr, TRUE, FALSE, L"Local\\Thermaltake500LCD.Stop");
    if (!stop) { ReleaseMutex(mutex); CloseHandle(mutex); throw std::runtime_error("Cannot create stop event"); }
    ResetEvent(stop);
    try {
        if (vendorRunning()) {
            log(L"TT RGB Plus is running. LCD remains under vendor control.");
        } else if (oneShot) {
            Device device;
            device.connect();
            auto image = stillImage(loadSettings());
            device.sendImage(encodeJpeg(*image));
            log(L"Still image transmitted.");
        } else {
            Monitor monitor;
            monitor.sample(); // Prime CPU usage calculation.
            std::unique_ptr<Device> device;
            auto lastHeartbeat = std::chrono::steady_clock::now();
            log(L"C++ LCD worker started.");
            while (WaitForSingleObject(stop, 0) != WAIT_OBJECT_0 && !vendorRunning()) {
                try {
                    if (!device) {
                        device = std::make_unique<Device>();
                        device->connect();
                        lastHeartbeat = std::chrono::steady_clock::now();
                        log(L"LCD connected.");
                    }
                    Settings settings = loadSettings();
                    Metrics current = monitor.sample();
                    auto image = render(settings, current);
                    device->sendImage(encodeJpeg(*image));
                    if (std::chrono::steady_clock::now() - lastHeartbeat >= std::chrono::seconds(2)) {
                        device->heartbeat();
                        lastHeartbeat = std::chrono::steady_clock::now();
                    }
                    DWORD delay = static_cast<DWORD>(std::clamp(settings.interval, 0.5, 60.0) * 1000);
                    if (sleepOrStop(stop, delay)) break;
                } catch (const std::exception& e) {
                    device.reset(); // Close both HID interfaces before retrying.
                    std::wstring reason(e.what(), e.what() + strlen(e.what()));
                    log(L"LCD error (reconnecting): " + reason);
                    if (sleepOrStop(stop, 3000)) break;
                }
            }
            log(vendorRunning() ? L"TT RGB Plus took control; stopped." : L"LCD worker stopped.");
        }
    } catch (...) {
        CloseHandle(stop);
        ReleaseMutex(mutex);
        CloseHandle(mutex);
        throw;
    }
    CloseHandle(stop);
    ReleaseMutex(mutex);
    CloseHandle(mutex);
    return 0;
}
} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    try {
        int argc = 0;
        LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        bool still = false;
        for (int i = 1; argv && i < argc; ++i) {
            if (wcscmp(argv[i], L"--still") == 0) still = true;
        }
        if (argv) LocalFree(argv);
        return run(still);
    } catch (const std::exception& e) {
        std::wstring message(e.what(), e.what() + strlen(e.what()));
        log(L"Fatal: " + message);
        return 1;
    } catch (...) {
        log(L"Fatal: unexpected exception");
        return 1;
    }
}
