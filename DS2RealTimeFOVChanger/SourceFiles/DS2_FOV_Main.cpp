#include <windows.h>
#include <iostream>
#include <cstdio>
#include <string>
#include <thread>
#include <chrono>
#include <algorithm>
#include <vector>
#include "config_file.h"
#include "fov_mod.h"
#include "globals.h"
#include "logger.h"

void SetModEnabled(bool enable) {
    Config::bModActive.store(enable);
    SetFOVHooksEnabled(enable);
    std::cout << "fov: mod toggled -> " << (enable ? "enabled" : "disabled") << "\n";
}

void ToggleMod() {
    SetModEnabled(!Config::bModActive.load());
}

struct KeyHoldState {
    bool isDown = false;
    std::chrono::steady_clock::time_point pressTime;
    std::chrono::steady_clock::time_point lastRepeatTime;
};

void KeyPollLoop() {
    static bool lastToggle = false;
    static bool lastReset = false;
    KeyHoldState incState;
    KeyHoldState decState;
    int configCheckCounter = 0;

    auto IsAnyKeyPressed = [](const std::vector<int>& keys) -> bool {
        for (int k : keys) {
            if (k > 0 && k < 256) {
                if ((GetAsyncKeyState(k) & 0x8000) != 0) return true;
            }
        }
        return false;
        };

    while (Config::bInitialized.load()) {
        if (++configCheckCounter >= 100) {
            configCheckCounter = 0;
            if (HasConfigChanged()) {
                std::cout << "config: external change detected, reloading\n";
                LoadConfig();
            }
        }

        if (Config::EnableHotkeys.load()) {
            bool toggleDown = (GetAsyncKeyState(Config::ToggleKey) & 0x8000) != 0;
            if (toggleDown && !lastToggle) {
                ToggleMod();
            }
            lastToggle = toggleDown;

            if (Config::bModActive.load()) {
                auto now = std::chrono::steady_clock::now();

                std::vector<int> incKeys = { Config::IncFOVKey };
                if (Config::IncFOVKey == VK_NUMPAD8) incKeys.push_back(VK_UP);

                std::vector<int> decKeys = { Config::DecFOVKey };
                if (Config::DecFOVKey == VK_NUMPAD5) decKeys.push_back(VK_CLEAR);

                std::vector<int> resetKeys = { Config::ResetFOVKey };
                if (Config::ResetFOVKey == VK_NUMPAD2) resetKeys.push_back(VK_DOWN);

                bool incDown = IsAnyKeyPressed(incKeys);
                bool decDown = IsAnyKeyPressed(decKeys);
                bool resetDown = IsAnyKeyPressed(resetKeys);

                if (resetDown && !lastReset) {
                    Config::TargetFOV.store(Config::DefaultFOV);
                    if (!Config::SmoothFOV) {
                        Config::CurrentFOV.store(Config::DefaultFOV);
                    }
                    SaveConfig();
                    std::cout << "hotkey: FOV Reset -> " << Config::DefaultFOV << " deg\n";
                }
                lastReset = resetDown;

                auto ProcessHold = [&](bool isPressed, KeyHoldState& state, float stepSign, const char* label) {
                    if (isPressed) {
                        if (!state.isDown) {
                            state.isDown = true;
                            state.pressTime = now;
                            state.lastRepeatTime = now;

                            float current = Config::TargetFOV.load();
                            float updated = std::clamp(current + stepSign * Config::Step, Config::MinFOV, Config::MaxFOV);
                            Config::TargetFOV.store(updated);
                        }
                        else if (Config::HoldContinuous) {
                            auto heldMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - state.pressTime).count();
                            if (heldMs >= Config::HoldDelayMs) {
                                auto intervalMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - state.lastRepeatTime).count();
                                if (intervalMs >= Config::HoldIntervalMs) {
                                    state.lastRepeatTime = now;
                                    float current = Config::TargetFOV.load();
                                    float updated = std::clamp(current + stepSign * Config::Step, Config::MinFOV, Config::MaxFOV);
                                    Config::TargetFOV.store(updated);
                                }
                            }
                        }
                    }
                    else if (state.isDown) {
                        state.isDown = false;
                        SaveConfig();
                        std::cout << "hotkey: " << label << " -> " << Config::TargetFOV.load() << " deg\n";
                    }
                    };

                ProcessHold(incDown, incState, 1.0f, "FOV (+)");
                ProcessHold(decDown, decState, -1.0f, "FOV (-)");
            }
        }

        static bool s_firstInterceptLogged = false;
        if (!s_firstInterceptLogged && Config::GlobalCameraPtr != 0) {
            s_firstInterceptLogged = true;
            std::cout << "hook: Intercepted IngameCameraOperator @ 0x"
                << std::hex << Config::GlobalCameraPtr << std::dec << "\n";
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

static void CreateDebugConsole() {
    if (!Config::EnableConsole) return;

    if (AllocConsole()) {
        FILE* fp;
        freopen_s(&fp, "CONOUT$", "w", stdout);
        freopen_s(&fp, "CONOUT$", "w", stderr);
        freopen_s(&fp, "CONIN$", "r", stdin);

        std::ios::sync_with_stdio(true);
        std::cout.clear();
        std::cerr.clear();
        std::cin.clear();

        SetConsoleTitleA("Dark Souls 2 Real-Time FOV Changer Debug Console");
    }
}

static DWORD WINAPI ModThread(LPVOID) {
    CreateDebugConsole();
    Logger::Initialize();

    Sleep(200);

    std::cout << "init: loading configuration\n";
    LoadConfig();

    std::cout << "hooks: scanning and applying fov hooks\n";
    ApplyFOVHooks();

    Config::bInitialized.store(true);
    std::cout << "main: entering hotkey polling loop\n\n";
    KeyPollLoop();

    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        Config::g_hModule = hModule;
        DisableThreadLibraryCalls(hModule);
        CreateThread(nullptr, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(ModThread), nullptr, 0, nullptr);
    }
    else if (reason == DLL_PROCESS_DETACH) {
        Config::bInitialized.store(false);
        SetFOVHooksEnabled(false);
        UninstallCameraHooks();
        Logger::Shutdown();
    }
    return TRUE;
}
