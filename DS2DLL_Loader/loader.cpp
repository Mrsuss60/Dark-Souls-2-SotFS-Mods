#include "loader.h"
#include <process.h>
#include <vector>
#include <fstream>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <algorithm>

namespace {
    HMODULE g_hLoaderModule = nullptr;
    HANDLE g_hThread = nullptr;
    std::vector<HMODULE> g_loadedMods;
    std::wstring g_logFilePath;

    std::wstring ToLower(const std::wstring& s) {
        std::wstring res = s;
        std::transform(res.begin(), res.end(), res.begin(), ::towlower);
        return res;
    }

    std::wstring GetGameDirectory() {
        wchar_t exePath[MAX_PATH];
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        std::wstring path(exePath);
        size_t lastSlash = path.find_last_of(L"\\/");
        if (lastSlash != std::wstring::npos) {
            return path.substr(0, lastSlash + 1);
        }
        return L"";
    }

    std::wstring GetModuleName(HMODULE hMod) {
        wchar_t modPath[MAX_PATH];
        GetModuleFileNameW(hMod, modPath, MAX_PATH);
        std::wstring path(modPath);
        size_t lastSlash = path.find_last_of(L"\\/");
        if (lastSlash != std::wstring::npos) {
            return path.substr(lastSlash + 1);
        }
        return path;
    }

    void LogMessage(const std::wstring& msg) {
        if (g_logFilePath.empty()) {
            g_logFilePath = GetGameDirectory() + L"DS2DLL_Loader.log";
        }

        std::wofstream log(g_logFilePath, std::ios::out | std::ios::app);
        if (!log.is_open()) return;

        auto now = std::chrono::system_clock::now();
        std::time_t nowTime = std::chrono::system_clock::to_time_t(now);
        std::tm tmBuf;
        if (localtime_s(&tmBuf, &nowTime) == 0) {
            log << L"[" << std::put_time(&tmBuf, L"%H:%M:%S") << L"] ";
        }
        log << msg << L"\n";
        log.flush();

        OutputDebugStringW((L"DS2DLL_Loader " + msg + L"\n").c_str());
    }

    bool WaitForGameWindow() {
        int attempts = 0;
        while (attempts < 50) {
            HWND hwnd = FindWindowW(L"DARK SOULS II", nullptr);
            if (!hwnd) hwnd = FindWindowW(nullptr, L"DARK SOULS II");
            if (hwnd) return true;
            Sleep(100);
            attempts++;
        }
        return false;
    }

    void LoadModFromPath(const std::wstring& modPath, const std::wstring& modName) {
        HMODULE hMod = LoadLibraryW(modPath.c_str());
        if (hMod) {
            g_loadedMods.push_back(hMod);
            LogMessage(L"Loaded mod: " + modName + L" @ 0x" + std::to_wstring(reinterpret_cast<uintptr_t>(hMod)));
        } else {
            DWORD err = GetLastError();
            LogMessage(L"Failed to load mod: " + modName + L" (Error: " + std::to_wstring(err) + L")");
        }
    }

    unsigned int __stdcall LoaderThread(void*) {
        std::wstring gameDir = GetGameDirectory();
        std::wstring loaderName = GetModuleName(g_hLoaderModule);
        g_logFilePath = gameDir + L"DS2DLL_Loader.log";

        {
            std::wofstream logInit(g_logFilePath, std::ios::out | std::ios::trunc);
            if (logInit.is_open()) {
                logInit << L"  Dark Souls 2 DS2DLL_Loader\n";
            }
        }

        LogMessage(L"Loader started as proxy: " + loaderName);
        LogMessage(L"Game directory: " + gameDir);

        if (WaitForGameWindow()) {
            LogMessage(L"Dark Souls II window detected.");
        } else {
            LogMessage(L"Wait timeout reached; proceeding with mod injection.");
        }

        constexpr DWORD kLoadDelayMs = 500;
        Sleep(kLoadDelayMs);

        std::wstring modsDir = gameDir + L"DS2Mods\\";
        DWORD modsAttr = GetFileAttributesW(modsDir.c_str());
        if (modsAttr == INVALID_FILE_ATTRIBUTES || !(modsAttr & FILE_ATTRIBUTE_DIRECTORY)) {
            CreateDirectoryW(modsDir.c_str(), nullptr);
            LogMessage(L"Created dedicated mods folder: " + modsDir);
        } else {
            LogMessage(L"Found dedicated mods folder: " + modsDir);
        }

        std::wstring fovDllPath = modsDir + L"DS2FOVChanger.dll";
        bool fovLoaded = false;
        if (GetFileAttributesW(fovDllPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
            LoadModFromPath(fovDllPath, L"DS2FOVChanger.dll");
            fovLoaded = true;
        } else {
            std::wstring rootFovPath = gameDir + L"DS2FOVChanger.dll";
            if (GetFileAttributesW(rootFovPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
                LogMessage(L"DS2FOVChanger.dll found in game root, loading fallback.");
                LoadModFromPath(rootFovPath, L"DS2FOVChanger.dll");
                fovLoaded = true;
            } else {
                LogMessage(L"Notice: DS2FOVChanger.dll not found in " + modsDir);
            }
        }
        std::wstring searchPattern = modsDir + L"*.dll";
        WIN32_FIND_DATAW findData;
        HANDLE hFind = FindFirstFileW(searchPattern.c_str(), &findData);

        if (hFind != INVALID_HANDLE_VALUE) {
            std::wstring myNameLower = ToLower(loaderName);
            do {
                if (!(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                    std::wstring currentDllName = findData.cFileName;
                    std::wstring lowerName = ToLower(currentDllName);

                    if (fovLoaded && lowerName == L"ds2fovchanger.dll") {
                        continue;
                    }
                    if (lowerName == myNameLower || lowerName == L"dinput8.dll" ||
                        lowerName == L"winmm.dll" || lowerName == L"dxgi.dll" ||
                        lowerName == L"d3d11.dll") {
                        continue;
                    }

                    std::wstring fullModPath = modsDir + currentDllName;
                    LoadModFromPath(fullModPath, currentDllName);
                }
            } while (FindNextFileW(hFind, &findData));
            FindClose(hFind);
        }

        LogMessage(L"Mod loading completed. Total active mods: " + std::to_wstring(g_loadedMods.size()));
        return 0;
    }
}

namespace Loader {
    void Initialize(HMODULE hModule) {
        g_hLoaderModule = hModule;
        g_hThread = reinterpret_cast<HANDLE>(_beginthreadex(nullptr, 0, LoaderThread, nullptr, 0, nullptr));
    }

    void Cleanup() {
        if (g_hThread) {
            WaitForSingleObject(g_hThread, 1000);
            CloseHandle(g_hThread);
            g_hThread = nullptr;
        }

        for (HMODULE hMod : g_loadedMods) {
            if (hMod) {
                FreeLibrary(hMod);
            }
        }
        g_loadedMods.clear();
    }
}
