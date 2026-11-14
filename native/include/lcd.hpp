#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace lcd {
namespace fs = std::filesystem;
inline constexpr int kWidth = 480;
inline constexpr int kHeight = 128;

struct Settings {
    std::wstring image;
    int rotation = 0;
    bool contain = false;
    double interval = 2.0;
    int brightness = 100;
    COLORREF accent = RGB(40, 222, 187);
    COLORREF text = RGB(242, 247, 250);
};
struct Metrics {
    double cpu = 0;
    double ram = 0;
    double gpu = -1;
};

fs::path dataDir();
fs::path imagesDir();
fs::path settingsFile();
void log(const std::wstring& line);
Settings loadSettings();
bool saveSettings(const Settings& settings);
fs::path resolveImage(const Settings& settings);
std::vector<std::wstring> imageNames();
std::wstring importImage(const fs::path& source);
std::wstring quoted(const std::wstring& value);
std::wstring errorMessage(DWORD error);

class Monitor {
public:
    Monitor();
    ~Monitor();
    Monitor(const Monitor&) = delete;
    Monitor& operator=(const Monitor&) = delete;
    Metrics sample();
private:
    HMODULE nvml_ = nullptr;
    void* gpuHandle_ = nullptr;
    bool initialized_ = false;
    using Shutdown = int (__cdecl*)();
    using Utilization = int (__cdecl*)(void*, void*);
    Shutdown shutdown_ = nullptr;
    Utilization utilization_ = nullptr;
    ULARGE_INTEGER oldIdle_{}, oldKernel_{}, oldUser_{};
    bool primed_ = false;
};

std::unique_ptr<Gdiplus::Bitmap> render(const Settings&, const Metrics&);
std::vector<unsigned char> encodeJpeg(Gdiplus::Image& image);
void savePng(Gdiplus::Image& image, const fs::path& file);
bool vendorRunning();
bool stopWorker();

class Device {
public:
    Device() = default;
    ~Device();
    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;
    void connect();
    void close();
    void sendImage(const std::vector<unsigned char>& jpeg);
    void heartbeat();
private:
    HANDLE command_ = INVALID_HANDLE_VALUE;
    HANDLE frame_ = INVALID_HANDLE_VALUE;
    DWORD commandIn_ = 0, frameIn_ = 0;
    void send(HANDLE handle, const std::vector<unsigned char>& bytes);
    void acknowledge(HANDLE handle, DWORD inputSize, DWORD timeoutMs = 2000);
    void initialize();
};

struct GdiplusSession {
    ULONG_PTR token = 0;
    GdiplusSession();
    ~GdiplusSession();
};
} // namespace lcd
