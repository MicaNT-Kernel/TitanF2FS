#include "titanf2fs/f2fs_engine.hpp"
#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>
#include <sstream>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace {
constexpr const wchar_t* SERVICE_NAME = L"TitanF2FS";
constexpr const wchar_t* DISPLAY_NAME = L"TitanF2FS Flash-Friendly Storage Daemon";
constexpr const wchar_t* PIPE_NAME    = L"\\\\.\\pipe\\titanf2fs_ipc";
constexpr uint64_t F2FS_IPC_MAGIC     = 0x4632465349504331ULL; // "F2FSIPC1"

SERVICE_STATUS        g_ServiceStatus{};
SERVICE_STATUS_HANDLE g_StatusHandle{nullptr};
std::atomic<bool>     g_Running{false};

#pragma pack(push, 1)
struct IpcHeader {
    uint64_t magic;
    uint32_t command;
    uint32_t payloadLen;
};

struct IpcResponseHeader {
    uint64_t magic;
    uint32_t status;
    uint32_t payloadLen;
};
#pragma pack(pop)

struct MountedEntry {
    std::string driveLetter;
    std::string devPath;
    uint64_t    totalBytes{0};
};
std::vector<MountedEntry> g_MountedList;

void HandlePipeClient(HANDLE hPipe) {
    IpcHeader reqHdr{};
    DWORD bytesRead = 0;
    if (!ReadFile(hPipe, &reqHdr, sizeof(reqHdr), &bytesRead, nullptr) || bytesRead != sizeof(reqHdr)) {
        CloseHandle(hPipe);
        return;
    }

    if (reqHdr.magic != F2FS_IPC_MAGIC) {
        CloseHandle(hPipe);
        return;
    }

    std::string payload;
    if (reqHdr.payloadLen > 0) {
        payload.resize(reqHdr.payloadLen);
        DWORD readPayload = 0;
        ReadFile(hPipe, payload.data(), reqHdr.payloadLen, &readPayload, nullptr);
    }

    std::string respStr;
    uint32_t status = 0;

    switch (reqHdr.command) {
        case 1: { // Probe
            respStr = "{\"service\":\"TitanF2FS\",\"version\":\"1.0.0\",\"status\":\"ready\"}";
            break;
        }
        case 2: { // Mount
            std::istringstream iss(payload);
            std::string dev, drive;
            iss >> dev >> drive;
            if (!dev.empty() && !drive.empty()) {
                MountedEntry me;
                me.devPath = dev;
                me.driveLetter = drive;
                me.totalBytes = 64ULL * 1024 * 1024;
                g_MountedList.push_back(me);
                respStr = "{\"status\":\"mounted\",\"drive\":\"" + drive + "\"}";
            } else {
                status = 1;
                respStr = "{\"error\":\"Invalid mount arguments\"}";
            }
            break;
        }
        case 3: { // Unmount
            std::string drive = payload;
            auto it = std::remove_if(g_MountedList.begin(), g_MountedList.end(),
                                     [&](const MountedEntry& e) { return e.driveLetter == drive; });
            if (it != g_MountedList.end()) {
                g_MountedList.erase(it, g_MountedList.end());
                respStr = "{\"status\":\"unmounted\",\"drive\":\"" + drive + "\"}";
            } else {
                respStr = "{\"status\":\"unmounted\"}";
            }
            break;
        }
        case 6: { // Status
            respStr = "{\"status\":\"running\",\"mountCount\":" + std::to_string(g_MountedList.size()) + "}";
            break;
        }
        case 7: { // List Mounts
            std::ostringstream oss;
            oss << "[";
            for (size_t i = 0; i < g_MountedList.size(); ++i) {
                if (i > 0) oss << ",";
                oss << "{\"drive\":\"" << g_MountedList[i].driveLetter << "\",\"device\":\""
                    << g_MountedList[i].devPath << "\",\"subvol\":\"\",\"totalBytes\":"
                    << g_MountedList[i].totalBytes << "}";
            }
            oss << "]";
            respStr = oss.str();
            break;
        }
        default: {
            status = 2;
            respStr = "{\"error\":\"Unknown command\"}";
            break;
        }
    }

    IpcResponseHeader respHdr{F2FS_IPC_MAGIC, status, static_cast<uint32_t>(respStr.size())};
    DWORD written = 0;
    WriteFile(hPipe, &respHdr, sizeof(respHdr), &written, nullptr);
    if (!respStr.empty()) {
        WriteFile(hPipe, respStr.data(), static_cast<DWORD>(respStr.size()), &written, nullptr);
    }

    FlushFileBuffers(hPipe);
    DisconnectNamedPipe(hPipe);
    CloseHandle(hPipe);
}

void IpcServerThread() {
    while (g_Running.load()) {
        HANDLE hPipe = CreateNamedPipeW(
            PIPE_NAME,
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES,
            65536, 65536, 1000,
            nullptr
        );

        if (hPipe == INVALID_HANDLE_VALUE) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            continue;
        }

        if (ConnectNamedPipe(hPipe, nullptr) ? TRUE : (GetLastError() == ERROR_PIPE_CONNECTED)) {
            std::thread(HandlePipeClient, hPipe).detach();
        } else {
            CloseHandle(hPipe);
        }
    }
}

void WINAPI ServiceCtrlHandler(DWORD ctrl) {
    if (ctrl == SERVICE_CONTROL_STOP || ctrl == SERVICE_CONTROL_SHUTDOWN) {
        g_Running.store(false);
        g_ServiceStatus.dwCurrentState = SERVICE_STOPPED;
        SetServiceStatus(g_StatusHandle, &g_ServiceStatus);
    }
}

void WINAPI ServiceMain(DWORD, LPWSTR*) {
    g_StatusHandle = RegisterServiceCtrlHandlerW(SERVICE_NAME, ServiceCtrlHandler);
    if (!g_StatusHandle) return;

    g_ServiceStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    g_ServiceStatus.dwCurrentState = SERVICE_RUNNING;
    g_ServiceStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;
    SetServiceStatus(g_StatusHandle, &g_ServiceStatus);

    g_Running.store(true);
    std::thread serverThread(IpcServerThread);

    while (g_Running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    if (serverThread.joinable()) {
        serverThread.join();
    }
}
} // namespace
#endif

int main(int argc, char* argv[]) {
    if (argc > 1) {
        std::string cmd = argv[1];
        if (cmd == "--help" || cmd == "-h" || cmd == "/?") {
            std::cout << "TitanF2FS Storage Daemon v1.0.0\n";
            std::cout << "Usage: titanf2fs_svc [--install | --uninstall | --run]\n";
            return 0;
        }
#ifdef _WIN32
        if (cmd == "--install") {
            wchar_t binPath[MAX_PATH];
            GetModuleFileNameW(nullptr, binPath, MAX_PATH);
            SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CREATE_SERVICE);
            if (!scm) { std::cerr << "[-] Error: Failed to open SCM.\n"; return 1; }
            SC_HANDLE svc = CreateServiceW(
                scm, SERVICE_NAME, DISPLAY_NAME, SERVICE_ALL_ACCESS,
                SERVICE_WIN32_OWN_PROCESS, SERVICE_AUTO_START, SERVICE_ERROR_NORMAL,
                binPath, nullptr, nullptr, nullptr, nullptr, nullptr
            );
            if (svc) {
                std::cout << "[+] TitanF2FS Service registered successfully.\n";
                CloseServiceHandle(svc);
            } else {
                std::cerr << "[-] Error: Failed to register service. Error: " << GetLastError() << "\n";
            }
            CloseServiceHandle(scm);
            return 0;
        } else if (cmd == "--uninstall") {
            SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
            if (!scm) return 1;
            SC_HANDLE svc = OpenServiceW(scm, SERVICE_NAME, DELETE);
            if (svc) {
                DeleteService(svc);
                CloseServiceHandle(svc);
                std::cout << "[+] TitanF2FS Service uninstalled.\n";
            }
            CloseServiceHandle(scm);
            return 0;
        } else if (cmd == "--run") {
            std::cout << "[+] Running TitanF2FS Daemon in interactive console mode...\n";
            std::cout << "[+] IPC Pipe: \\\\.\\pipe\\titanf2fs_ipc\n";
            g_Running.store(true);
            std::thread t(IpcServerThread);
            t.join();
            return 0;
        }
#endif
    }

#ifdef _WIN32
    SERVICE_TABLE_ENTRYW serviceTable[] = {
        {const_cast<LPWSTR>(SERVICE_NAME), ServiceMain},
        {nullptr, nullptr}
    };
    StartServiceCtrlDispatcherW(serviceTable);
#else
    std::cout << "TitanF2FS service runs as a Windows NT service.\n";
#endif
    return 0;
}
