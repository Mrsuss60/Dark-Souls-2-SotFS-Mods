#pragma once
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <string>

namespace Config {
    inline HMODULE g_hModule = nullptr;

    inline std::string GetModDirectory() {
        static std::string s_modDir;
        if (!s_modDir.empty()) return s_modDir;

        HMODULE hMod = g_hModule;
        if (!hMod) {
            GetModuleHandleExA(
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCSTR>(&GetModDirectory),
                &hMod);
        }

        char path[MAX_PATH] = { 0 };
        if (GetModuleFileNameA(hMod, path, MAX_PATH) > 0) {
            std::string fullPath = path;
            size_t pos = fullPath.find_last_of("\\/");
            if (pos != std::string::npos) {
                s_modDir = fullPath.substr(0, pos + 1);
                return s_modDir;
            }
        }
        return ".\\";
    }

    inline std::string GetModFilePath(const std::string& filename) {
        return GetModDirectory() + filename;
    }

    inline std::atomic<float> TargetFOV = 43.0f;
    inline std::atomic<float> CurrentFOV = 43.0f;
    inline float DefaultFOV = 43.0f;

    inline float Step = 0.25f;
    inline float MinFOV = 10.0f;
    inline float MaxFOV = 125.0f;

    inline bool SmoothFOV = true;
    inline float SmoothSpeed = 5.00f;

    inline bool HoldContinuous = true;
    inline constexpr int HoldDelayMs = 10;
    inline constexpr int HoldIntervalMs = 20;

    inline std::atomic<bool> EnableHotkeys = true;
    inline bool EnableConsole = false;

    inline int ToggleKey = 0x76;      
    inline int IncFOVKey = 0x68;      
    inline int DecFOVKey = 0x65;      
    inline int ResetFOVKey = 0x62;      

    inline uint32_t Offset_IngameNormalCam = 0x130;
    inline uint32_t Offset_IngameActionCam = 0x280;
    inline uint32_t Offset_IngameDefaultCam = 0x340;
    inline uint32_t Offset_IngameSubjectiveCam = 0x400;
    inline uint32_t Offset_IngameExFollowCam = 0x500;
    inline uint32_t Offset_IngameActiveState = 0xD0;

    inline uint32_t Offset_LiveFOV = 0x90;  
    inline uint32_t Offset_CameraParamPtr = 0xB8;  
    inline uint32_t Offset_ParamFOV = 0x04;  

    inline uint32_t Offset_ExFollowParamPtr = 0x560; 
    inline uint32_t Offset_ExFollowFOV = 0x04;  

    inline const char* SigGameManagerInit = "BA 01 00 00 00 48 8B CB E8 ?? ?? ?? ?? BA 02 00 00 00 48 8B CB E8 ?? ?? ?? ?? BA 03 00 00 00 48 8B CB E8 ?? ?? ?? ?? BA 04 00 00 00";

    inline uintptr_t RvaIngameCameraUpdate = 0x00495B60;
    inline uintptr_t RvaPlayerCameraUpdate = 0x00497130;
    inline uintptr_t RvaNormalCameraUpdate = 0x004A2700;
    inline uintptr_t RvaExFollowCameraUpdate = 0x0049AFB0;
    inline uintptr_t RvaBuildProjMatrix = 0x00001A90;

    inline std::atomic<bool> bModActive = true;
    inline std::atomic<bool> bInitialized = false;
    inline std::atomic<bool> bFOVActive = true;

    inline uintptr_t GlobalCameraPtr = 0;
    inline uintptr_t GlobalGameManagerPtr = 0;

    inline uintptr_t AddrCameraHook = 0;
    inline uintptr_t AddrCameraCodecave = 0;
    inline size_t HookLen = 0;
    inline unsigned char OrigBytes[32] = { 0 };
    inline unsigned char PatchJmp[32] = { 0 };
}