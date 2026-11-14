#include "lcd.hpp"

#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cwchar>
#include <exception>
#include <memory>
#include <string>
#include <vector>

#pragma comment(lib, "comctl32.lib")

using namespace lcd;

namespace {
constexpr int kIdStart = 100, kIdStop = 101, kIdImport = 102, kIdClear = 103;
constexpr int kIdStill = 104, kIdImages = 105, kIdRotation = 106, kIdInterval = 107;
constexpr int kIdFit = 108, kIdBrightness = 109, kIdAccent = 110, kIdText = 111;
constexpr COLORREF kBackground = RGB(16, 26, 36);
constexpr COLORREF kForeground = RGB(242, 247, 250);
constexpr COLORREF kMuted = RGB(156, 184, 199);
struct App {
    HWND window = nullptr;
    HWND preview = nullptr;
    HWND images = nullptr, rotation = nullptr, fit = nullptr, interval = nullptr;
    HWND brightness = nullptr, brightnessValue = nullptr, status = nullptr;
    HFONT font = nullptr, titleFont = nullptr;
    HBRUSH background = nullptr, inputBrush = nullptr;
    Settings settings;
    std::unique_ptr<Monitor> monitor;
    Metrics lastMetrics;
    bool loading = true;
    bool active = false;
};
App app;

HWND makeControl(const wchar_t* kind, const wchar_t* label, DWORD style, DWORD extra,
                 int x, int y, int w, int h, int id = 0) {
    auto child = CreateWindowExW(extra, kind, label, style | WS_CHILD | WS_VISIBLE,
                                x, y, w, h, app.window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                GetModuleHandleW(nullptr), nullptr);
    SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(app.font), TRUE);
    return child;
}
HWND label(const wchar_t* text, int x, int y, int w, int h) {
    return makeControl(L"STATIC", text, SS_LEFT, 0, x, y, w, h);
}
HWND button(const wchar_t* text, int id, int x, int y, int w, int h) {
    return makeControl(L"BUTTON", text, BS_PUSHBUTTON | WS_TABSTOP, 0, x, y, w, h, id);
}
void setStatus(const std::wstring& message) {
    if (app.status) SetWindowTextW(app.status, message.c_str());
}
std::wstring windowText(HWND control) {
    int length = GetWindowTextLengthW(control);
    std::wstring value(static_cast<size_t>(length) + 1, L'\0');
    GetWindowTextW(control, value.data(), length + 1);
    value.resize(length);
    return value;
}
bool workerRunning() {
    HANDLE mutex = OpenMutexW(SYNCHRONIZE | MUTEX_MODIFY_STATE, FALSE, L"Local\\Thermaltake500LCD.Worker");
    if (!mutex) return false;
    DWORD result = WaitForSingleObject(mutex, 0);
    if (result == WAIT_OBJECT_0 || result == WAIT_ABANDONED) ReleaseMutex(mutex);
    CloseHandle(mutex);
    return result == WAIT_TIMEOUT;
}
std::wstring workerPath() {
    wchar_t file[MAX_PATH]{};
    GetModuleFileNameW(nullptr, file, MAX_PATH);
    return (fs::path(file).parent_path() / L"thermaltake500lcd.service.exe").wstring();
}
bool startWorker(const wchar_t* argument) {
    auto path = workerPath();
    if (!fs::is_regular_file(path)) {
        setStatus(L"Missing thermaltake500lcd.service.exe beside this app.");
        return false;
    }
    std::wstring command = quoted(path);
    if (argument && *argument) { command += L" "; command += argument; }
    std::vector<wchar_t> writable(command.begin(), command.end());
    writable.push_back(L'\0');
    STARTUPINFOW startup{}; startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    BOOL started = CreateProcessW(path.c_str(), writable.data(), nullptr, nullptr, FALSE,
                                  CREATE_NO_WINDOW, nullptr, fs::path(path).parent_path().c_str(),
                                  &startup, &process);
    if (!started) { setStatus(L"Unable to launch LCD worker: " + errorMessage(GetLastError())); return false; }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
}
bool validateAndSave() {
    auto value = windowText(app.interval);
    wchar_t* end = nullptr;
    double seconds = wcstod(value.c_str(), &end);
    if (end == value.c_str() || *end != L'\0' || !std::isfinite(seconds) || seconds < 0.5 || seconds > 60) {
        MessageBoxW(app.window, L"Refresh must be between 0.5 and 60 seconds.", L"Thermaltake 500 LCD", MB_OK | MB_ICONWARNING);
        return false;
    }
    app.settings.interval = seconds;
    app.settings.rotation = static_cast<int>(SendMessageW(app.rotation, CB_GETCURSEL, 0, 0)) * 90;
    app.settings.contain = SendMessageW(app.fit, CB_GETCURSEL, 0, 0) == 1;
    app.settings.brightness = static_cast<int>(SendMessageW(app.brightness, TBM_GETPOS, 0, 0));
    if (!saveSettings(app.settings)) {
        setStatus(L"Could not write settings under LocalAppData.");
        return false;
    }
    return true;
}
void updatePreview() {
    if (!app.window) return;
    if (app.monitor) app.lastMetrics = app.monitor->sample();
    InvalidateRect(app.window, nullptr, FALSE);
}
void refreshImages() {
    SendMessageW(app.images, CB_RESETCONTENT, 0, 0);
    SendMessageW(app.images, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"(No background)"));
    int index = 0, selected = 0;
    for (const auto& name : imageNames()) {
        ++index;
        SendMessageW(app.images, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name.c_str()));
        if (fs::path(app.settings.image).filename().wstring() == name) selected = index;
    }
    SendMessageW(app.images, CB_SETCURSEL, selected, 0);
}
void importPicture() {
    wchar_t name[32768]{};
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = app.window;
    dialog.lpstrFile = name;
    dialog.nMaxFile = static_cast<DWORD>(std::size(name));
    dialog.lpstrFilter = L"Images\0*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.webp\0All files\0*.*\0";
    dialog.nFilterIndex = 1;
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    if (GetOpenFileNameW(&dialog)) {
        try {
            app.settings.image = importImage(fs::path(name));
            refreshImages();
            validateAndSave();
            updatePreview();
            setStatus(L"Image imported. Click Start to apply it to the LCD.");
        } catch (const std::exception& e) {
            std::wstring message(e.what(), e.what() + strlen(e.what()));
            MessageBoxW(app.window, message.c_str(), L"Image import failed", MB_OK | MB_ICONERROR);
        }
    }
}
void chooseColor(COLORREF& color) {
    COLORREF custom[16]{};
    CHOOSECOLORW picker{};
    picker.lStructSize = sizeof(picker);
    picker.hwndOwner = app.window;
    picker.rgbResult = color;
    picker.lpCustColors = custom;
    picker.Flags = CC_FULLOPEN | CC_RGBINIT;
    if (ChooseColorW(&picker)) {
        color = picker.rgbResult;
        validateAndSave();
        updatePreview();
        setStatus(L"Color saved. Click Start to apply it to the LCD.");
    }
}
void onCommand(int id, int notification) {
    if (app.loading) return;
    try {
        switch (id) {
        case kIdStart:
            if (!validateAndSave()) break;
            stopWorker();
            if (startWorker(L"--run")) { app.active = true; setStatus(L"Starting live dashboard..."); }
            break;
        case kIdStop:
            if (stopWorker()) setStatus(L"Stop requested; last image will remain on LCD.");
            else setStatus(L"No C++ LCD worker is running.");
            app.active = false;
            break;
        case kIdImport: importPicture(); break;
        case kIdClear:
            app.settings.image.clear();
            refreshImages();
            validateAndSave();
            updatePreview();
            setStatus(L"Background cleared. Click Start to apply.");
            break;
        case kIdStill:
            if (resolveImage(app.settings).empty()) {
                MessageBoxW(app.window, L"Import and select an image first.", L"Thermaltake 500 LCD", MB_OK);
                break;
            }
            if (!validateAndSave()) break;
            stopWorker();
            if (startWorker(L"--still")) setStatus(L"Sending still image...");
            app.active = false;
            break;
        case kIdImages:
            if (notification != CBN_SELCHANGE) break;
            {
                int selected = static_cast<int>(SendMessageW(app.images, CB_GETCURSEL, 0, 0));
                if (selected <= 0) app.settings.image.clear();
                else app.settings.image = L"images\\" + windowText(app.images);
                validateAndSave(); updatePreview();
                setStatus(L"Image selected. Click Start to apply.");
            }
            break;
        case kIdRotation:
        case kIdFit:
            if (notification == CBN_SELCHANGE) { validateAndSave(); updatePreview(); }
            break;
        case kIdAccent: chooseColor(app.settings.accent); break;
        case kIdText: chooseColor(app.settings.text); break;
        default: break;
        }
    } catch (const std::exception& e) {
        std::wstring message(e.what(), e.what() + strlen(e.what()));
        setStatus(L"Error: " + message);
        log(L"GUI error: " + message);
    }
}
void initializeUi() {
    app.background = CreateSolidBrush(kBackground);
    app.inputBrush = CreateSolidBrush(RGB(31, 44, 57));
    app.font = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                           OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    app.titleFont = CreateFontW(-23, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    HWND title = label(L"Thermaltake 3.9-inch LCD", 22, 17, 480, 32);
    SendMessageW(title, WM_SETFONT, reinterpret_cast<WPARAM>(app.titleFont), TRUE);
    label(L"480 x 128   |   USB 264A:233D", 22, 54, 450, 20);
    label(L"GPU Load: NVIDIA GPU 0 (RTX 5090)", 22, 72, 480, 20);
    button(L"Start live dashboard", kIdStart, 22, 252, 171, 32);
    button(L"Stop", kIdStop, 203, 252, 76, 32);
    button(L"Import image...", kIdImport, 22, 294, 132, 32);
    button(L"Clear background", kIdClear, 164, 294, 135, 32);
    button(L"Send still image only", kIdStill, 309, 294, 181, 32);
    label(L"Image library", 22, 346, 98, 23);
    app.images = makeControl(WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP,
                             0, 125, 342, 365, 260, kIdImages);
    label(L"Rotate", 22, 393, 54, 24);
    app.rotation = makeControl(WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_TABSTOP,
                               0, 80, 389, 96, 200, kIdRotation);
    for (const auto* degrees : {L"0°", L"90°", L"180°", L"270°"})
        SendMessageW(app.rotation, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(degrees));
    label(L"Refresh every", 211, 393, 105, 24);
    app.interval = makeControl(L"EDIT", L"2", ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP,
                               WS_EX_CLIENTEDGE, 318, 389, 50, 26, kIdInterval);
    label(L"seconds", 377, 393, 100, 24);
    label(L"Image fit", 22, 433, 80, 24);
    app.fit = makeControl(WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_TABSTOP,
                          0, 108, 429, 195, 160, kIdFit);
    SendMessageW(app.fit, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Crop to fill"));
    SendMessageW(app.fit, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Show whole image"));
    label(L"Brightness", 22, 474, 90, 24);
    app.brightness = makeControl(TRACKBAR_CLASSW, L"", TBS_NOTICKS | WS_TABSTOP,
                                 0, 107, 465, 287, 33, kIdBrightness);
    SendMessageW(app.brightness, TBM_SETRANGE, FALSE, MAKELPARAM(0, 100));
    app.brightnessValue = label(L"100%", 404, 474, 75, 23);
    label(L"Dims image pixels; LCD backlight stays on.", 22, 502, 460, 19);
    button(L"Accent color...", kIdAccent, 22, 532, 140, 32);
    button(L"Main text color...", kIdText, 172, 532, 164, 32);
    app.status = label(L"Ready.", 22, 581, 483, 40);

    app.loading = true;
    refreshImages();
    SendMessageW(app.rotation, CB_SETCURSEL, app.settings.rotation / 90, 0);
    SendMessageW(app.fit, CB_SETCURSEL, app.settings.contain ? 1 : 0, 0);
    wchar_t intervalText[32]{};
    swprintf_s(intervalText, L"%.1f", app.settings.interval);
    SetWindowTextW(app.interval, intervalText);
    SendMessageW(app.brightness, TBM_SETPOS, TRUE, app.settings.brightness);
    SetWindowTextW(app.brightnessValue, (std::to_wstring(app.settings.brightness) + L"%").c_str());
    app.loading = false;
    app.active = workerRunning();
    setStatus(app.active ? L"Live dashboard is running." : L"Ready.");
    SetTimer(app.window, 1, 2000, nullptr);
    updatePreview();
}
LRESULT CALLBACK wndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_CREATE:
        app.window = hwnd;
        initializeUi();
        return 0;
    case WM_COMMAND:
        onCommand(LOWORD(wparam), HIWORD(wparam));
        return 0;
    case WM_HSCROLL:
        if (reinterpret_cast<HWND>(lparam) == app.brightness && !app.loading) {
            app.settings.brightness = static_cast<int>(SendMessageW(app.brightness, TBM_GETPOS, 0, 0));
            SetWindowTextW(app.brightnessValue, (std::to_wstring(app.settings.brightness) + L"%").c_str());
            saveSettings(app.settings);
            updatePreview();
        }
        return 0;
    case WM_TIMER:
        updatePreview();
        if (app.active && !workerRunning()) {
            app.active = false;
            setStatus(L"LCD worker stopped. Check native-dashboard.log.");
        }
        return 0;
    case WM_CTLCOLORSTATIC:
        SetTextColor(reinterpret_cast<HDC>(wparam), kForeground);
        SetBkColor(reinterpret_cast<HDC>(wparam), kBackground);
        return reinterpret_cast<LRESULT>(app.background);
    case WM_CTLCOLOREDIT:
        SetTextColor(reinterpret_cast<HDC>(wparam), kForeground);
        SetBkColor(reinterpret_cast<HDC>(wparam), RGB(31, 44, 57));
        return reinterpret_cast<LRESULT>(app.inputBrush);
    case WM_ERASEBKGND: {
        RECT rect{}; GetClientRect(hwnd, &rect);
        FillRect(reinterpret_cast<HDC>(wparam), &rect, app.background);
        return 1;
    }
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(hwnd, &paint);
        try {
            Gdiplus::Graphics graphics(dc);
            graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            Gdiplus::Pen border(Gdiplus::Color(255, 52, 84, 102), 1);
            graphics.DrawRectangle(&border, 21, 103, 481, 129);
            auto frame = render(app.settings, app.lastMetrics);
            graphics.DrawImage(frame.get(), Gdiplus::Rect(22, 104, 480, 128));
        } catch (const std::exception& e) {
            log(L"Preview error: " + std::wstring(e.what(), e.what() + strlen(e.what())));
        }
        EndPaint(hwnd, &paint);
        return 0;
    }
    case WM_DESTROY:
        KillTimer(hwnd, 1);
        if (app.font) DeleteObject(app.font);
        if (app.titleFont) DeleteObject(app.titleFont);
        if (app.background) DeleteObject(app.background);
        if (app.inputBrush) DeleteObject(app.inputBrush);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}
} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    try {
        GdiplusSession gdiplus;
        INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_BAR_CLASSES | ICC_STANDARD_CLASSES};
        InitCommonControlsEx(&controls);
        app.settings = loadSettings();
        app.monitor = std::make_unique<Monitor>();
        app.monitor->sample();
        WNDCLASSEXW cls{};
        cls.cbSize = sizeof(cls);
        cls.hInstance = instance;
        cls.lpfnWndProc = wndProc;
        cls.lpszClassName = L"Thermaltake500LCDControl";
        cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        cls.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
        if (!RegisterClassExW(&cls)) return 1;
        HWND window = CreateWindowExW(0, cls.lpszClassName, L"Thermaltake500LCD",
                                    WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                                    CW_USEDEFAULT, CW_USEDEFAULT, 540, 661, nullptr, nullptr, instance, nullptr);
        if (!window) return 1;
        ShowWindow(window, show);
        UpdateWindow(window);
        MSG message{};
        while (GetMessageW(&message, nullptr, 0, 0) > 0) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        app.monitor.reset();
        return static_cast<int>(message.wParam);
    } catch (const std::exception& e) {
        std::wstring message(e.what(), e.what() + strlen(e.what()));
        MessageBoxW(nullptr, message.c_str(), L"Thermaltake500LCD error", MB_OK | MB_ICONERROR);
        return 1;
    }
}
