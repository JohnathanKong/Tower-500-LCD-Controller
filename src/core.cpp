#include "lcd.hpp"

#include <commctrl.h>
#include <hidsdi.h>
#include <hidpi.h>
#include <setupapi.h>
#include <tlhelp32.h>
#include <objidl.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cwctype>
#include <fstream>
#include <iomanip>
#include <locale>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <vector>

namespace lcd {
namespace {
std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    int count = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (count <= 0) return {};
    std::wstring out(count, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), count);
    return out;
}
std::string narrow(const std::wstring& s) {
    if (s.empty()) return {};
    int count = WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
    if (count <= 0) return {};
    std::string out(count, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), count, nullptr, nullptr);
    return out;
}
std::string jsonEscape(const std::wstring& value) {
    std::string out;
    for (unsigned char c : narrow(value)) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default: if (c >= 0x20) out += static_cast<char>(c); break;
        }
    }
    return out;
}
std::string jsonString(const std::string& data, const std::string& name) {
    const std::regex pattern("\"" + name + "\"\\s*:\\s*\"((?:\\\\.|[^\"\\\\])*)\"");
    std::smatch match;
    if (!std::regex_search(data, match, pattern)) return {};
    const auto& encoded = match[1].str();
    std::string result;
    for (size_t i = 0; i < encoded.size(); ++i) {
        if (encoded[i] != '\\' || i + 1 >= encoded.size()) { result += encoded[i]; continue; }
        char next = encoded[++i];
        switch (next) {
        case 'n': result += '\n'; break;
        case 'r': result += '\r'; break;
        case 't': result += '\t'; break;
        case '"': result += '"'; break;
        case '\\': result += '\\'; break;
        case '/': result += '/'; break;
        default: result += next; break;
        }
    }
    return result;
}
double jsonNumber(const std::string& data, const std::string& name, double fallback) {
    const std::regex pattern("\"" + name + "\"\\s*:\\s*\"?(-?[0-9]+(?:\\.[0-9]+)?)");
    std::smatch match;
    if (std::regex_search(data, match, pattern)) {
        try { return std::stod(match[1].str()); } catch (...) {}
    }
    return fallback;
}
std::wstring colorHex(COLORREF value) {
    wchar_t buffer[12]{};
    swprintf_s(buffer, L"#%02X%02X%02X", GetRValue(value), GetGValue(value), GetBValue(value));
    return buffer;
}
COLORREF parseColor(const std::string& color, COLORREF fallback) {
    if (color.size() != 7 || color[0] != '#') return fallback;
    try {
        unsigned long v = std::stoul(color.substr(1), nullptr, 16);
        return RGB((v >> 16) & 255, (v >> 8) & 255, v & 255);
    } catch (...) { return fallback; }
}
Gdiplus::Color asColor(COLORREF value, BYTE alpha = 255) {
    return Gdiplus::Color(alpha, GetRValue(value), GetGValue(value), GetBValue(value));
}
std::wstring lower(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
    return s;
}
void roundedRect(Gdiplus::Graphics& g, const Gdiplus::RectF& r, float radius, const Gdiplus::Color& color) {
    Gdiplus::GraphicsPath path;
    float d = radius * 2;
    path.AddArc(r.X, r.Y, d, d, 180, 90);
    path.AddArc(r.GetRight() - d, r.Y, d, d, 270, 90);
    path.AddArc(r.GetRight() - d, r.GetBottom() - d, d, d, 0, 90);
    path.AddArc(r.X, r.GetBottom() - d, d, d, 90, 90);
    path.CloseFigure();
    Gdiplus::SolidBrush brush(color);
    g.FillPath(&brush, &path);
}
void centered(Gdiplus::Graphics& g, const std::wstring& value, float x, float y, float w,
              float size, COLORREF foreground, bool bold) {
    Gdiplus::FontFamily family(L"Segoe UI");
    Gdiplus::Font font(&family, size, bold ? Gdiplus::FontStyleBold : Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    Gdiplus::StringFormat format;
    format.SetAlignment(Gdiplus::StringAlignmentCenter);
    Gdiplus::RectF area(x, y, w, size * 1.8f);
    Gdiplus::SolidBrush shadow(Gdiplus::Color(235, 0, 0, 0));
    for (int dx = -1; dx <= 1; ++dx)
        for (int dy = -1; dy <= 1; ++dy)
            if (dx || dy) g.DrawString(value.c_str(), -1, &font,
                 Gdiplus::RectF(x + dx, y + dy, w, size * 1.8f), &format, &shadow);
    Gdiplus::SolidBrush brush(asColor(foreground));
    g.DrawString(value.c_str(), -1, &font, area, &format, &brush);
}
void gauge(Gdiplus::Graphics& g, float x, float y, float w, float h, double percent, COLORREF accent) {
    roundedRect(g, Gdiplus::RectF(x, y, w, h), 16, Gdiplus::Color(177, 5, 12, 20));
    float radius = (std::min(w - 8, h - 8) - 7) / 2.f;
    float cx = x + w / 2.f, cy = y + h / 2.f;
    Gdiplus::RectF arc(cx - radius, cy - radius, radius * 2, radius * 2);
    Gdiplus::Pen track(Gdiplus::Color(155, 255, 255, 255), 6.f);
    track.SetStartCap(Gdiplus::LineCapRound);
    track.SetEndCap(Gdiplus::LineCapRound);
    g.DrawArc(&track, arc, 135.f, 270.f);
    if (percent > 0) {
        Gdiplus::Pen colored(asColor(accent), 7.f);
        colored.SetStartCap(Gdiplus::LineCapRound);
        colored.SetEndCap(Gdiplus::LineCapRound);
        g.DrawArc(&colored, arc, 135.f, static_cast<float>(270 * std::clamp(percent, 0.0, 100.0) / 100));
    }
}
bool filesEqual(const fs::path& a, const fs::path& b) {
    std::error_code ec;
    if (fs::file_size(a, ec) != fs::file_size(b, ec) || ec) return false;
    std::ifstream x(a, std::ios::binary), y(b, std::ios::binary);
    if (!x || !y) return false;
    std::array<char, 4096> bufA{}, bufB{};
    while (x) {
        x.read(bufA.data(), bufA.size());
        y.read(bufB.data(), bufB.size());
        if (x.gcount() != y.gcount() || !std::equal(bufA.begin(), bufA.begin() + x.gcount(), bufB.begin()))
            return false;
    }
    return true;
}
int encoderClsid(const wchar_t* mime, CLSID& result) {
    UINT count = 0, size = 0;
    if (Gdiplus::GetImageEncodersSize(&count, &size) != Gdiplus::Ok || !size) return 0;
    std::vector<BYTE> bytes(size);
    auto* encoders = reinterpret_cast<Gdiplus::ImageCodecInfo*>(bytes.data());
    if (Gdiplus::GetImageEncoders(count, size, encoders) != Gdiplus::Ok) return 0;
    for (UINT i = 0; i < count; ++i) {
        if (wcscmp(encoders[i].MimeType, mime) == 0) { result = encoders[i].Clsid; return 1; }
    }
    return 0;
}
DWORD capsInput(HANDLE file, DWORD expectedOutput) {
    PHIDP_PREPARSED_DATA prepared = nullptr;
    HIDP_CAPS caps{};
    if (!HidD_GetPreparsedData(file, &prepared)) throw std::runtime_error("HidD_GetPreparsedData failed");
    NTSTATUS status = HidP_GetCaps(prepared, &caps);
    HidD_FreePreparsedData(prepared);
    if (status != HIDP_STATUS_SUCCESS || caps.OutputReportByteLength != expectedOutput + 1 || caps.InputReportByteLength < 1)
        throw std::runtime_error("Unexpected HID report sizes");
    return caps.InputReportByteLength;
}
void checkHandle(HANDLE h) { if (h == INVALID_HANDLE_VALUE) throw std::runtime_error("LCD HID device could not be opened"); }
} // namespace

fs::path dataDir() {
    DWORD required = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
    if (required <= 1) throw std::runtime_error("LOCALAPPDATA is not set");
    std::wstring value(required, L'\0');
    GetEnvironmentVariableW(L"LOCALAPPDATA", value.data(), required);
    value.resize(wcslen(value.c_str()));
    auto path = fs::path(value) / L"Tower500LCD";
    fs::create_directories(path / L"images");
    return path;
}
fs::path imagesDir() { return dataDir() / L"images"; }
fs::path settingsFile() { return dataDir() / L"lcd-settings.json"; }

void log(const std::wstring& line) {
    try {
        SYSTEMTIME now{}; GetLocalTime(&now);
        wchar_t stamp[48]{};
        swprintf_s(stamp, L"[%04d-%02d-%02d %02d:%02d:%02d] ",
                   now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);
        std::ofstream out(dataDir() / L"native-dashboard.log", std::ios::binary | std::ios::app);
        out << narrow(std::wstring(stamp) + line + L"\n");
    } catch (...) {}
}
std::wstring errorMessage(DWORD code) {
    LPWSTR buffer = nullptr;
    DWORD length = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, code, 0, reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
    std::wstring result = length && buffer ? std::wstring(buffer, length) : L"Windows error " + std::to_wstring(code);
    if (buffer) LocalFree(buffer);
    return result;
}
Settings loadSettings() {
    Settings s;
    try {
        std::ifstream in(settingsFile(), std::ios::binary);
        if (!in) return s;
        std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        s.image = widen(jsonString(data, "image"));
        int rotation = static_cast<int>(jsonNumber(data, "rotation", 0));
        if (rotation == 0 || rotation == 90 || rotation == 180 || rotation == 270) s.rotation = rotation;
        s.contain = jsonString(data, "fit") == "contain";
        auto seconds = jsonNumber(data, "interval", 2);
        if (std::isfinite(seconds) && seconds >= 0.5 && seconds <= 60) s.interval = seconds;
        auto brightness = jsonNumber(data, "brightness", 100);
        if (brightness >= 0 && brightness <= 100) s.brightness = static_cast<int>(brightness);
        s.accent = parseColor(jsonString(data, "accent"), s.accent);
        s.text = parseColor(jsonString(data, "textColor"), s.text);
    } catch (const std::exception& e) { log(L"Settings read failed: " + widen(e.what())); }
    return s;
}
bool saveSettings(const Settings& s) {
    try {
        std::ostringstream out;
        out.imbue(std::locale::classic());
        out << "{\n  \"image\": \"" << jsonEscape(s.image)
            << "\",\n  \"rotation\": " << s.rotation
            << ",\n  \"fit\": \"" << (s.contain ? "contain" : "cover")
            << "\",\n  \"interval\": " << s.interval
            << ",\n  \"brightness\": " << s.brightness
            << ",\n  \"accent\": \"" << narrow(colorHex(s.accent))
            << "\",\n  \"textColor\": \"" << narrow(colorHex(s.text))
            << "\"\n}\n";
        const auto temp = dataDir() / L"lcd-settings.tmp";
        std::ofstream file(temp, std::ios::binary | std::ios::trunc);
        if (!file) return false;
        file << out.str(); file.close();
        return MoveFileExW(temp.c_str(), settingsFile().c_str(),
                          MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
    } catch (const std::exception& e) { log(L"Settings write failed: " + widen(e.what())); return false; }
}
fs::path resolveImage(const Settings& s) {
    if (s.image.empty()) return {};
    fs::path path(s.image);
    if (path.is_relative()) path = dataDir() / path;
    if (!fs::exists(path)) {
        const auto old = fs::path(s.image);
        if (old.is_relative()) return {};
    }
    return fs::exists(path) ? path : fs::path{};
}
std::vector<std::wstring> imageNames() {
    std::vector<std::wstring> names;
    for (const auto& entry : fs::directory_iterator(imagesDir())) {
        if (!entry.is_regular_file()) continue;
        auto ext = lower(entry.path().extension().wstring());
        if (ext == L".png" || ext == L".jpg" || ext == L".jpeg" ||
            ext == L".bmp" || ext == L".gif" || ext == L".webp")
            names.push_back(entry.path().filename().wstring());
    }
    std::sort(names.begin(), names.end());
    return names;
}
std::wstring importImage(const fs::path& source) {
    if (!fs::is_regular_file(source)) throw std::runtime_error("Source image not found");
    const auto names = imageNames();
    for (const auto& name : names) {
        if (filesEqual(source, imagesDir() / name)) return L"images\\" + name;
    }
    auto candidate = source.filename();
    for (int i = 1; fs::exists(imagesDir() / candidate); ++i)
        candidate = fs::path(source.stem().wstring() + L"-" + std::to_wstring(i) + source.extension().wstring());
    fs::copy_file(source, imagesDir() / candidate);
    return L"images\\" + candidate.wstring();
}
std::wstring quoted(const std::wstring& value) {
    std::wstring result = L"\"";
    unsigned slashes = 0;
    for (wchar_t c : value) {
        if (c == L'\\') { ++slashes; continue; }
        if (c == L'\"') { result.append(slashes * 2 + 1, L'\\'); result += c; }
        else { result.append(slashes, L'\\'); result += c; }
        slashes = 0;
    }
    result.append(slashes * 2, L'\\');
    return result + L"\"";
}
GdiplusSession::GdiplusSession() {
    Gdiplus::GdiplusStartupInput input;
    if (Gdiplus::GdiplusStartup(&token, &input, nullptr) != Gdiplus::Ok)
        throw std::runtime_error("GDI+ initialization failed");
}
GdiplusSession::~GdiplusSession() { if (token) Gdiplus::GdiplusShutdown(token); }

Monitor::Monitor() {
    nvml_ = LoadLibraryW(L"nvml.dll");
    if (!nvml_) return;
    auto init = reinterpret_cast<int (__cdecl*)()>(GetProcAddress(nvml_, "nvmlInit_v2"));
    auto handle = reinterpret_cast<int (__cdecl*)(unsigned, void**)>(GetProcAddress(nvml_, "nvmlDeviceGetHandleByIndex_v2"));
    shutdown_ = reinterpret_cast<Shutdown>(GetProcAddress(nvml_, "nvmlShutdown"));
    utilization_ = reinterpret_cast<Utilization>(GetProcAddress(nvml_, "nvmlDeviceGetUtilizationRates"));
    if (init && handle && shutdown_ && utilization_ && init() == 0) {
        initialized_ = true;
        if (handle(0, &gpuHandle_) != 0) gpuHandle_ = nullptr;
    }
}
Monitor::~Monitor() {
    if (initialized_ && shutdown_) shutdown_();
    if (nvml_) FreeLibrary(nvml_);
}
Metrics Monitor::sample() {
    Metrics out;
    MEMORYSTATUSEX mem{};
    mem.dwLength = sizeof(mem);
    if (GlobalMemoryStatusEx(&mem)) out.ram = static_cast<double>(mem.dwMemoryLoad);
    FILETIME idle{}, kernel{}, user{};
    if (GetSystemTimes(&idle, &kernel, &user)) {
        auto to64 = [](const FILETIME& t) {
            ULARGE_INTEGER u{}; u.LowPart = t.dwLowDateTime; u.HighPart = t.dwHighDateTime; return u;
        };
        auto i = to64(idle), k = to64(kernel), u = to64(user);
        if (primed_) {
            ULONGLONG di = i.QuadPart - oldIdle_.QuadPart;
            ULONGLONG dk = k.QuadPart - oldKernel_.QuadPart;
            ULONGLONG du = u.QuadPart - oldUser_.QuadPart;
            if (dk + du) out.cpu = std::clamp(100. * (1. - double(di) / double(dk + du)), 0., 100.);
        }
        oldIdle_ = i; oldKernel_ = k; oldUser_ = u; primed_ = true;
    }
    if (gpuHandle_ && utilization_) {
        struct GpuLoad { unsigned gpu, memory; } usage{};
        if (utilization_(gpuHandle_, &usage) == 0) out.gpu = usage.gpu;
    }
    return out;
}

std::unique_ptr<Gdiplus::Bitmap> render(const Settings& s, const Metrics& metrics) {
    const bool portrait = s.rotation == 90 || s.rotation == 270;
    const int width = portrait ? kHeight : kWidth;
    const int height = portrait ? kWidth : kHeight;
    auto image = std::make_unique<Gdiplus::Bitmap>(width, height, PixelFormat32bppARGB);
    Gdiplus::Graphics g(image.get());
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
    g.Clear(Gdiplus::Color(255, 7, 16, 25));
    auto imagePath = resolveImage(s);
    if (!imagePath.empty()) {
        Gdiplus::Bitmap bg(imagePath.c_str());
        if (bg.GetLastStatus() == Gdiplus::Ok && bg.GetWidth() && bg.GetHeight()) {
            float sw = static_cast<float>(bg.GetWidth()), sh = static_cast<float>(bg.GetHeight());
            float scale = s.contain ? std::min(width / sw, height / sh) : std::max(width / sw, height / sh);
            float dw = sw * scale, dh = sh * scale;
            g.DrawImage(&bg, Gdiplus::RectF((width - dw) / 2, (height - dh) / 2, dw, dh));
        }
    }
    SYSTEMTIME now{}; GetLocalTime(&now);
    wchar_t clockText[32]{};
    swprintf_s(clockText, L"%d:%02d", (now.wHour + 11) % 12 + 1, now.wMinute);
    struct Item { const wchar_t* name; double value; };
    Item items[] = {{L"CPU", metrics.cpu}, {L"GPU", metrics.gpu}, {L"RAM", metrics.ram}};
    if (portrait) {
        centered(g, clockText, 8, 20, 112, 30, s.text, true);
        for (int i = 0; i < 3; ++i) {
            float y = static_cast<float>(84 + i * 134);
            gauge(g, 5, y, 118, 112, items[i].value, s.accent);
            centered(g, items[i].name, 10, y + 24, 108, 18, RGB(255, 255, 255), true);
            wchar_t value[32]{};
            if (items[i].value < 0) wcscpy_s(value, L"N/A");
            else swprintf_s(value, L"%.0f%%", items[i].value);
            centered(g, value, 10, y + 53, 108, 30, s.text, true);
        }
    } else {
        centered(g, clockText, 5, 20, 110, 26, s.text, true);
        for (int i = 0; i < 3; ++i) {
            float x = static_cast<float>(122 + i * 122);
            gauge(g, x, 5, i == 2 ? 112 : 114, 118, items[i].value, s.accent);
            centered(g, items[i].name, x + 4, 29, i == 2 ? 104 : 106, 16, RGB(255, 255, 255), true);
            wchar_t value[32]{};
            if (items[i].value < 0) wcscpy_s(value, L"N/A");
            else swprintf_s(value, L"%.0f%%", items[i].value);
            centered(g, value, x + 4, 58, i == 2 ? 104 : 106, 26, s.text, true);
        }
    }
    g.Flush();
    Gdiplus::RotateFlipType flip = Gdiplus::RotateNoneFlipNone;
    if (s.rotation == 90) flip = Gdiplus::Rotate90FlipNone;
    if (s.rotation == 180) flip = Gdiplus::Rotate180FlipNone;
    if (s.rotation == 270) flip = Gdiplus::Rotate270FlipNone;
    image->RotateFlip(flip);
    if (s.brightness < 100) {
        const auto amount = static_cast<unsigned>(std::clamp(s.brightness, 0, 100));
        Gdiplus::Rect bounds(0, 0, image->GetWidth(), image->GetHeight());
        Gdiplus::BitmapData data{};
        if (image->LockBits(&bounds, Gdiplus::ImageLockModeWrite, PixelFormat32bppARGB, &data) == Gdiplus::Ok) {
            for (UINT y = 0; y < image->GetHeight(); ++y) {
                auto* row = static_cast<BYTE*>(data.Scan0) + static_cast<ptrdiff_t>(y) * data.Stride;
                for (UINT x = 0; x < image->GetWidth(); ++x)
                    for (int channel = 0; channel < 3; ++channel)
                        row[x * 4 + channel] = static_cast<BYTE>((row[x * 4 + channel] * amount) / 100);
            }
            image->UnlockBits(&data);
        }
    }
    return image;
}
std::vector<unsigned char> encodeJpeg(Gdiplus::Image& image) {
    CLSID clsid{};
    if (!encoderClsid(L"image/jpeg", clsid)) throw std::runtime_error("No JPEG encoder found");
    IStream* stream = nullptr;
    if (CreateStreamOnHGlobal(nullptr, TRUE, &stream) != S_OK)
        throw std::runtime_error("Could not allocate JPEG stream");
    ULONG quality = 88;
    Gdiplus::EncoderParameters params{};
    params.Count = 1;
    params.Parameter[0].Guid = Gdiplus::EncoderQuality;
    params.Parameter[0].Type = Gdiplus::EncoderParameterValueTypeLong;
    params.Parameter[0].NumberOfValues = 1;
    params.Parameter[0].Value = &quality;
    auto status = image.Save(stream, &clsid, &params);
    HGLOBAL global = nullptr;
    if (status != Gdiplus::Ok || GetHGlobalFromStream(stream, &global) != S_OK) {
        stream->Release(); throw std::runtime_error("JPEG encoding failed");
    }
    SIZE_T size = GlobalSize(global);
    void* bytes = GlobalLock(global);
    if (!bytes) { stream->Release(); throw std::runtime_error("JPEG memory lock failed"); }
    std::vector<unsigned char> result(static_cast<unsigned char*>(bytes), static_cast<unsigned char*>(bytes) + size);
    GlobalUnlock(global);
    stream->Release();
    return result;
}
void savePng(Gdiplus::Image& image, const fs::path& file) {
    CLSID clsid{};
    if (!encoderClsid(L"image/png", clsid) || image.Save(file.c_str(), &clsid, nullptr) != Gdiplus::Ok)
        throw std::runtime_error("PNG preview could not be saved");
}
bool vendorRunning() {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W entry{}; entry.dwSize = sizeof(entry);
    bool found = false;
    if (Process32FirstW(snapshot, &entry)) do {
        auto name = lower(entry.szExeFile);
        if (name.rfind(L"ttrgbplus", 0) == 0 || name.rfind(L"tt rgb plus", 0) == 0) {
            found = true; break;
        }
    } while (Process32NextW(snapshot, &entry));
    CloseHandle(snapshot);
    return found;
}
bool stopWorker() {
    HANDLE event = OpenEventW(EVENT_MODIFY_STATE, FALSE, L"Local\\Thermaltake500LCD.Stop");
    if (!event) return false;
    BOOL signaled = SetEvent(event);
    CloseHandle(event);
    return signaled != 0;
}

Device::~Device() { close(); }
void Device::close() {
    if (command_ != INVALID_HANDLE_VALUE) { CloseHandle(command_); command_ = INVALID_HANDLE_VALUE; }
    if (frame_ != INVALID_HANDLE_VALUE) { CloseHandle(frame_); frame_ = INVALID_HANDLE_VALUE; }
}
void Device::send(HANDLE handle, const std::vector<unsigned char>& data) {
    std::vector<unsigned char> report(1 + data.size(), 0);
    std::copy(data.begin(), data.end(), report.begin() + 1);
    OVERLAPPED overlapped{};
    overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!overlapped.hEvent) throw std::runtime_error("Could not create HID write event");
    DWORD written = 0;
    BOOL ok = WriteFile(handle, report.data(), static_cast<DWORD>(report.size()), &written, &overlapped);
    if (!ok && GetLastError() == ERROR_IO_PENDING) {
        if (WaitForSingleObject(overlapped.hEvent, 2000) == WAIT_OBJECT_0)
            ok = GetOverlappedResult(handle, &overlapped, &written, FALSE);
        else { CancelIoEx(handle, &overlapped); GetOverlappedResult(handle, &overlapped, &written, TRUE); }
    }
    CloseHandle(overlapped.hEvent);
    if (!ok || written != report.size()) throw std::runtime_error("HID write failed or timed out");
}
void Device::acknowledge(HANDLE handle, DWORD inputSize, DWORD timeoutMs) {
    std::vector<BYTE> input(inputSize);
    OVERLAPPED overlapped{};
    overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!overlapped.hEvent) throw std::runtime_error("Could not create HID input event");
    DWORD count = 0;
    BOOL ok = ReadFile(handle, input.data(), inputSize, &count, &overlapped);
    if (!ok && GetLastError() == ERROR_IO_PENDING) {
        if (WaitForSingleObject(overlapped.hEvent, timeoutMs) == WAIT_OBJECT_0)
            ok = GetOverlappedResult(handle, &overlapped, &count, FALSE);
        else { CancelIoEx(handle, &overlapped); GetOverlappedResult(handle, &overlapped, &count, TRUE); }
    }
    CloseHandle(overlapped.hEvent);
    if (!ok || count == 0) throw std::runtime_error("HID acknowledgment failed or timed out");
}
void Device::connect() {
    close();
    GUID guid{};
    HidD_GetHidGuid(&guid);
    HDEVINFO devices = SetupDiGetClassDevsW(&guid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (devices == INVALID_HANDLE_VALUE) throw std::runtime_error("Could not enumerate HID interfaces");
    std::wstring commandPath, framePath;
    for (DWORD i = 0;; ++i) {
        SP_DEVICE_INTERFACE_DATA iface{}; iface.cbSize = sizeof(iface);
        if (!SetupDiEnumDeviceInterfaces(devices, nullptr, &guid, i, &iface)) break;
        DWORD size = 0;
        SetupDiGetDeviceInterfaceDetailW(devices, &iface, nullptr, 0, &size, nullptr);
        std::vector<BYTE> storage(size);
        auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(storage.data());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if (!SetupDiGetDeviceInterfaceDetailW(devices, &iface, detail, size, nullptr, nullptr)) continue;
        auto path = lower(detail->DevicePath);
        if (path.find(L"vid_264a") == std::wstring::npos || path.find(L"pid_233d") == std::wstring::npos) continue;
        if (path.find(L"&mi_00") != std::wstring::npos) commandPath = detail->DevicePath;
        if (path.find(L"&mi_01") != std::wstring::npos) framePath = detail->DevicePath;
    }
    SetupDiDestroyDeviceInfoList(devices);
    if (commandPath.empty() || framePath.empty())
        throw std::runtime_error("Thermaltake LCD 264A:233D interfaces MI_00/MI_01 not found");
    try {
        command_ = CreateFileW(commandPath.c_str(), GENERIC_READ | GENERIC_WRITE,
                               FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
        checkHandle(command_);
        frame_ = CreateFileW(framePath.c_str(), GENERIC_READ | GENERIC_WRITE,
                             FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
        checkHandle(frame_);
        commandIn_ = capsInput(command_, 440);
        frameIn_ = capsInput(frame_, 1024);
        initialize();
    } catch (...) { close(); throw; }
}
void Device::initialize() {
    for (BYTE opcode : {BYTE(0x85), BYTE(0x87), BYTE(0x85), BYTE(0x87), BYTE(0x84), BYTE(0x81)}) {
        std::vector<unsigned char> command(440, 0);
        command[0] = opcode; command[1] = 1; command[3] = 0x80;
        send(command_, command);
        acknowledge(command_, commandIn_);
    }
    std::vector<unsigned char> brightness(440, 0);
    brightness[0] = 0x12; brightness[1] = 1; brightness[3] = 0x80; brightness[4] = 100;
    send(command_, brightness);
}
void Device::heartbeat() {
    std::vector<unsigned char> command(440, 0);
    command[0] = 0x82; command[1] = 1; command[3] = 0x80;
    send(command_, command);
}
void Device::sendImage(const std::vector<unsigned char>& jpeg) {
    if (jpeg.size() < 4 || jpeg[0] != 0xff || jpeg[1] != 0xd8 ||
        jpeg[jpeg.size() - 2] != 0xff || jpeg.back() != 0xd9)
        throw std::runtime_error("Invalid JPEG frame");
    const size_t count = (jpeg.size() + 1019) / 1020;
    if (count == 0 || count > 255) throw std::runtime_error("LCD frame is too large");
    for (size_t i = 0; i < count; ++i) {
        std::vector<unsigned char> packet(1024, 0);
        packet[0] = 0x08;
        packet[1] = static_cast<BYTE>(i == 0 ? count : i);
        packet[3] = static_cast<BYTE>(i == 0 ? 0x80 : 0);
        auto start = i * 1020;
        auto length = std::min<size_t>(1020, jpeg.size() - start);
        std::copy_n(jpeg.begin() + static_cast<ptrdiff_t>(start), length, packet.begin() + 4);
        send(frame_, packet);
    }
    acknowledge(frame_, frameIn_);
}
} // namespace lcd
