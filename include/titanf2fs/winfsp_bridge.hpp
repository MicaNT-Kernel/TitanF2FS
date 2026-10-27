#pragma once

#include "titanf2fs/f2fs_engine.hpp"
#include <string>
#include <memory>
#include <vector>
#include <iostream>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace titanf2fs {

class WinFspApi {
public:
    static WinFspApi& Get() {
        static WinFspApi instance;
        return instance;
    }

    bool isAvailable() const noexcept { return hDll_ != nullptr; }

private:
    WinFspApi() {
        hDll_ = LoadLibraryW(L"winfsp-x64.dll");
        if (!hDll_) {
            HKEY hKey = nullptr;
            if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WinFsp", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
                wchar_t installDir[MAX_PATH]{0};
                DWORD bytes = sizeof(installDir);
                if (RegQueryValueExW(hKey, L"InstallDir", nullptr, nullptr, reinterpret_cast<LPBYTE>(installDir), &bytes) == ERROR_SUCCESS) {
                    std::wstring dllPath = std::wstring(installDir) + L"bin\\winfsp-x64.dll";
                    hDll_ = LoadLibraryW(dllPath.c_str());
                }
                RegCloseKey(hKey);
            }
        }
    }

    ~WinFspApi() {
        if (hDll_) {
            FreeLibrary(hDll_);
            hDll_ = nullptr;
        }
    }

    HMODULE hDll_{nullptr};
};

class WinFspMountSession {
public:
    WinFspMountSession(std::shared_ptr<F2fsEngine> engine, const std::wstring& driveLetter)
        : engine_(std::move(engine)), driveLetter_(driveLetter), isMounted_(false) {}

    bool Start() {
        if (!engine_ || !engine_->IsOpen()) return false;
        isMounted_ = true;
        return true;
    }

    void Stop() {
        isMounted_ = false;
    }

    [[nodiscard]] bool IsMounted() const noexcept { return isMounted_; }
    [[nodiscard]] const std::wstring& DriveLetter() const noexcept { return driveLetter_; }

private:
    std::shared_ptr<F2fsEngine> engine_;
    std::wstring driveLetter_;
    bool isMounted_{false};
};

} // namespace titanf2fs
#endif
