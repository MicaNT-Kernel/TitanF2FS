#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <shellapi.h>
#include <shlobj.h>
#include <gdiplus.h>

#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <thread>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <memory>
#include <cstdint>
#include <cstring>

#include "titanf2fs/compression.hpp"

namespace fs = std::filesystem;

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")

namespace {

#pragma pack(push, 1)
struct PayloadFooter {
    uint64_t tocOffset{0};
    uint64_t payloadStart{0};
    char     magic[16]{0};
};
#pragma pack(pop)

struct EmbeddedFile {
    std::string name;
    uint64_t    dataOffset{0};
    uint64_t    compSize{0};
    uint64_t    uncompSize{0};
    uint32_t    isCompressed{0};
};

std::vector<EmbeddedFile> g_Toc;
uint64_t g_PayloadStartOffset{0};
std::wstring g_SelfExePath;

bool ReadEmbeddedToc() {
    g_Toc.clear();
    std::ifstream in(fs::path(g_SelfExePath), std::ios::binary);
    if (!in.is_open()) return false;

    in.seekg(0, std::ios::end);
    uint64_t fileSize = in.tellg();
    if (fileSize < sizeof(PayloadFooter)) return false;

    in.seekg(fileSize - sizeof(PayloadFooter), std::ios::beg);
    PayloadFooter footer{};
    in.read(reinterpret_cast<char*>(&footer), sizeof(footer));

    if (std::memcmp(footer.magic, "TITANF2FS_BUNDLE", 16) != 0) return false;
    g_PayloadStartOffset = footer.payloadStart;

    in.seekg(footer.tocOffset, std::ios::beg);
    uint32_t fileCount = 0;
    in.read(reinterpret_cast<char*>(&fileCount), 4);
    if (fileCount == 0 || fileCount > 100) return false;

    for (uint32_t i = 0; i < fileCount; ++i) {
        uint32_t nameLen = 0;
        in.read(reinterpret_cast<char*>(&nameLen), 4);
        std::string name(nameLen, '\0');
        in.read(name.data(), nameLen);

        EmbeddedFile ef;
        ef.name = std::move(name);
        in.read(reinterpret_cast<char*>(&ef.dataOffset), 8);
        in.read(reinterpret_cast<char*>(&ef.compSize), 8);
        in.read(reinterpret_cast<char*>(&ef.uncompSize), 8);
        in.read(reinterpret_cast<char*>(&ef.isCompressed), 4);
        g_Toc.push_back(std::move(ef));
    }
    return !g_Toc.empty();
}

bool ExtractEmbeddedFile(const EmbeddedFile& ef, const std::wstring& outPath) {
    std::ifstream in(fs::path(g_SelfExePath), std::ios::binary);
    if (!in.is_open()) return false;

    in.seekg(ef.dataOffset, std::ios::beg);
    std::vector<uint8_t> compData(ef.compSize);
    in.read(reinterpret_cast<char*>(compData.data()), ef.compSize);

    std::vector<uint8_t> uncompData;
    if (ef.isCompressed) {
        if (!titanf2fs::CompressionEngine::DecompressLZ4(compData, uncompData)) {
            return false;
        }
    } else {
        uncompData = std::move(compData);
    }

    std::ofstream out(fs::path(outPath), std::ios::binary);
    if (!out.is_open()) return false;
    out.write(reinterpret_cast<const char*>(uncompData.data()), uncompData.size());
    return true;
}

constexpr int ID_BTN_INSTALL = 1001;
constexpr int ID_PROGRESS    = 1002;
constexpr int ID_STATUS_TXT  = 1003;

HWND g_hWndMain{nullptr};
HWND g_hBtnInstall{nullptr};
HWND g_hProgressBar{nullptr};
HWND g_hStatusText{nullptr};

ULONG_PTR g_gdiplusToken{0};
std::unique_ptr<Gdiplus::Image> g_HeroImage;
HFONT g_hFontTitle{nullptr};
HFONT g_hFontNormal{nullptr};
HFONT g_hFontStatus{nullptr};
HBRUSH g_hBrushBg{nullptr};

std::atomic<bool> g_Installing{false};
std::atomic<int>  g_ProgressValue{0};
std::wstring      g_StatusString = L"Ready to install TitanF2FS Storage Suite.";

void UpdateStatus(const std::wstring& text, int progress) {
    g_StatusString = text;
    g_ProgressValue.store(progress);
    if (g_hWndMain) {
        PostMessageW(g_hProgressBar, PBM_SETPOS, progress, 0);
        InvalidateRect(g_hWndMain, nullptr, TRUE);
    }
}

void RunInstallationWorker() {
    g_Installing.store(true);
    UpdateStatus(L"Preparing environment...", 5);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    wchar_t progFiles[MAX_PATH];
    SHGetFolderPathW(nullptr, CSIDL_PROGRAM_FILES, nullptr, 0, progFiles);
    std::wstring targetDir = std::wstring(progFiles) + L"\\TitanF2FS";

    try {
        fs::create_directories(targetDir);
    } catch (...) {}

    UpdateStatus(L"Extracting TitanF2FS engine and tools...", 15);
    for (size_t i = 0; i < g_Toc.size(); ++i) {
        const auto& ef = g_Toc[i];
        if (ef.name.find(".msi") != std::string::npos || ef.name.find(".jpg") != std::string::npos) {
            continue;
        }

        std::wstring destFile = targetDir + L"\\" + std::wstring(ef.name.begin(), ef.name.end());
        UpdateStatus(L"Deploying " + std::wstring(ef.name.begin(), ef.name.end()) + L"...", 20 + static_cast<int>(i * 8));
        ExtractEmbeddedFile(ef, destFile);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // Install WinFsp runtime if present
    for (const auto& ef : g_Toc) {
        if (ef.name.find(".msi") != std::string::npos) {
            wchar_t tempPath[MAX_PATH];
            GetTempPathW(MAX_PATH, tempPath);
            std::wstring msiDest = std::wstring(tempPath) + L"winfsp_f2fs_setup.msi";

            UpdateStatus(L"Extracting WinFsp runtime installer...", 60);
            if (ExtractEmbeddedFile(ef, msiDest)) {
                UpdateStatus(L"Configuring WinFsp user-mode driver...", 75);
                std::wstring cmd = L"msiexec.exe /i \"" + msiDest + L"\" /qn /norestart";
                STARTUPINFOW si{sizeof(si)};
                PROCESS_INFORMATION pi{};
                if (CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
                    WaitForSingleObject(pi.hProcess, 60000);
                    CloseHandle(pi.hProcess);
                    CloseHandle(pi.hThread);
                }
                DeleteFileW(msiDest.c_str());
            }
            break;
        }
    }

    // Register Background Service
    UpdateStatus(L"Registering TitanF2FS Windows NT service...", 85);
    std::wstring svcPath = targetDir + L"\\titanf2fs_svc.exe";
    std::wstring svcCmd = L"\"" + svcPath + L"\" --install";
    STARTUPINFOW si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    if (CreateProcessW(nullptr, svcCmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, 5000);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }

    // Add to System PATH
    UpdateStatus(L"Updating Windows PATH environment...", 95);
    HKEY hEnvKey = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment", 0, KEY_READ | KEY_WRITE, &hEnvKey) == ERROR_SUCCESS) {
        wchar_t currentPath[4096]{0};
        DWORD bytes = sizeof(currentPath);
        if (RegQueryValueExW(hEnvKey, L"Path", nullptr, nullptr, reinterpret_cast<LPBYTE>(currentPath), &bytes) == ERROR_SUCCESS) {
            std::wstring sPath(currentPath);
            if (sPath.find(targetDir) == std::wstring::npos) {
                sPath += L";" + targetDir;
                RegSetValueExW(hEnvKey, L"Path", 0, REG_EXPAND_SZ, reinterpret_cast<const BYTE*>(sPath.c_str()), static_cast<DWORD>((sPath.size() + 1) * sizeof(wchar_t)));
            }
        }
        RegCloseKey(hEnvKey);
    }

    UpdateStatus(L"Installation complete! TitanF2FS is active.", 100);
    g_Installing.store(false);
    EnableWindow(g_hBtnInstall, TRUE);
    SetWindowTextW(g_hBtnInstall, L"Finish");
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            g_hBrushBg = CreateSolidBrush(RGB(15, 17, 23));
            g_hFontTitle = CreateFontW(26, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            g_hFontNormal = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            g_hFontStatus = CreateFontW(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Consolas");

            // Hero banner extraction for display
            for (const auto& ef : g_Toc) {
                if (ef.name.find(".jpg") != std::string::npos || ef.name.find(".png") != std::string::npos) {
                    wchar_t tempPath[MAX_PATH];
                    GetTempPathW(MAX_PATH, tempPath);
                    std::wstring imgDest = std::wstring(tempPath) + L"titanf2fs_hero.jpg";
                    if (ExtractEmbeddedFile(ef, imgDest)) {
                        g_HeroImage = std::make_unique<Gdiplus::Image>(imgDest.c_str());
                    }
                    break;
                }
            }

            g_hProgressBar = CreateWindowExW(0, PROGRESS_CLASSW, nullptr, WS_CHILD | WS_VISIBLE | PBS_SMOOTH, 40, 385, 620, 22, hwnd, reinterpret_cast<HMENU>(ID_PROGRESS), GetModuleHandleW(nullptr), nullptr);
            SendMessageW(g_hProgressBar, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
            SendMessageW(g_hProgressBar, PBM_SETPOS, 0, 0);

            g_hBtnInstall = CreateWindowExW(0, L"BUTTON", L"Install TitanF2FS", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 480, 420, 180, 40, hwnd, reinterpret_cast<HMENU>(ID_BTN_INSTALL), GetModuleHandleW(nullptr), nullptr);
            SendMessageW(g_hBtnInstall, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFontNormal), TRUE);
            return 0;
        }

        case WM_COMMAND: {
            if (LOWORD(wParam) == ID_BTN_INSTALL) {
                wchar_t btnText[64]{0};
                GetWindowTextW(g_hBtnInstall, btnText, 64);
                if (std::wstring(btnText) == L"Finish") {
                    DestroyWindow(hwnd);
                    return 0;
                }
                if (!g_Installing.load()) {
                    EnableWindow(g_hBtnInstall, FALSE);
                    std::thread(RunInstallationWorker).detach();
                }
            }
            return 0;
        }

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rc;
            GetClientRect(hwnd, &rc);
            FillRect(hdc, &rc, g_hBrushBg);

            if (g_HeroImage && g_HeroImage->GetLastStatus() == Gdiplus::Ok) {
                Gdiplus::Graphics graphics(hdc);
                graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
                graphics.DrawImage(g_HeroImage.get(), 0, 0, 700, 270);
            }

            SetBkMode(hdc, TRANSPARENT);
            SelectObject(hdc, g_hFontTitle);
            SetTextColor(hdc, RGB(0, 210, 255));
            RECT rcTitle{40, 280, 660, 315};
            DrawTextW(hdc, L"TitanF2FS — Flash-Friendly File System for Windows NT", -1, &rcTitle, DT_LEFT | DT_SINGLELINE);

            SelectObject(hdc, g_hFontNormal);
            SetTextColor(hdc, RGB(200, 205, 215));
            RECT rcSub{40, 318, 660, 370};
            DrawTextW(hdc, L"Clean-room userland storage engine, log-structured scrubber, WinFsp mount bridge, and NT service for Samsung F2FS flash drives and NVMe SSDs.", -1, &rcSub, DT_LEFT | DT_WORDBREAK);

            SelectObject(hdc, g_hFontStatus);
            SetTextColor(hdc, RGB(0, 210, 255));
            RECT rcStatus{40, 425, 460, 460};
            DrawTextW(hdc, g_StatusString.c_str(), -1, &rcStatus, DT_LEFT | DT_SINGLELINE | DT_VCENTER);

            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_DESTROY: {
            if (g_hBrushBg) DeleteObject(g_hBrushBg);
            if (g_hFontTitle) DeleteObject(g_hFontTitle);
            if (g_hFontNormal) DeleteObject(g_hFontNormal);
            if (g_hFontStatus) DeleteObject(g_hFontStatus);
            PostQuitMessage(0);
            return 0;
        }
        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

} // namespace

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow) {
    wchar_t exePath[MAX_PATH]{0};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    g_SelfExePath = exePath;

    ReadEmbeddedToc();

    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    Gdiplus::GdiplusStartup(&g_gdiplusToken, &gdiplusStartupInput, nullptr);

    INITCOMMONCONTROLSEX icex{sizeof(INITCOMMONCONTROLSEX), ICC_PROGRESS_CLASS};
    InitCommonControlsEx(&icex);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = L"TitanF2FSSetupWindow";
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(
        WS_EX_APPWINDOW,
        L"TitanF2FSSetupWindow",
        L"TitanF2FS v1.0.0 Setup",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 700, 520,
        nullptr, nullptr, hInstance, nullptr
    );

    if (!hwnd) return 1;
    g_hWndMain = hwnd;

    // Enable Windows 11 Dark Mode Titlebar
    BOOL darkMode = TRUE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &darkMode, sizeof(darkMode));

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    Gdiplus::GdiplusShutdown(g_gdiplusToken);
    return static_cast<int>(msg.wParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    return wWinMain(hInstance, nullptr, GetCommandLineW(), nCmdShow);
}
