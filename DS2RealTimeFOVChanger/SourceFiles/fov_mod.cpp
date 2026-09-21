#include "fov_mod.h"
#include "globals.h"
#include "g_memory.h"
#include <iostream>
#include <iomanip>
#include <cmath>

static const uint32_t s_CameraSubOffsets[] = {
    Config::Offset_IngameNormalCam,      // 0x130
    Config::Offset_IngameActionCam,      // 0x280 (lock-on)
    Config::Offset_IngameDefaultCam,     // 0x340
    Config::Offset_IngameExFollowCam     // 0x500
};

static HookRecord g_hookIngameCam;
static HookRecord g_hookPlayerCam;
static HookRecord g_hookNormalCam;
static HookRecord g_hookExFollowCam;
static HookRecord g_hookBuildProjMatrix;

typedef __int64 (__fastcall *tIngameCameraUpdate)(uintptr_t thisPtr);
static tIngameCameraUpdate s_origIngameCameraUpdate = nullptr;

typedef __int64 (__fastcall *tPlayerCameraUpdate)(uintptr_t thisPtr, float deltaTime);
static tPlayerCameraUpdate s_origPlayerCameraUpdate = nullptr;

typedef __int64 (__fastcall *tNormalCameraUpdate)(uintptr_t thisPtr, float deltaTime);
static tNormalCameraUpdate s_origNormalCameraUpdate = nullptr;

typedef __int64 (__fastcall *tExFollowCameraUpdate)(uintptr_t thisPtr, float deltaTime);
static tExFollowCameraUpdate s_origExFollowCameraUpdate = nullptr;

typedef __int64 (__fastcall *tBuildProjMatrix)(uintptr_t outMatrix, float fov, float aspect, float nearClip, float farClip);
static tBuildProjMatrix s_origBuildProjMatrix = nullptr;

static bool s_hooksInstalled = false;

void UpdateFOVSmoothing(float deltaTime) {
    float target = Config::TargetFOV.load(std::memory_order_relaxed);
    if (!Config::SmoothFOV) {
        Config::CurrentFOV.store(target, std::memory_order_relaxed);
        return;
    }

    float current = Config::CurrentFOV.load(std::memory_order_relaxed);
    if (std::abs(target - current) < 0.001f) {
        Config::CurrentFOV.store(target, std::memory_order_relaxed);
        return;
    }

    if (deltaTime <= 0.0f || deltaTime > 0.1f) deltaTime = 0.0166f;
    float factor = 1.0f - std::exp(-Config::SmoothSpeed * deltaTime);
    float next = current + (target - current) * factor;
    Config::CurrentFOV.store(next, std::memory_order_relaxed);
}

static __int64 __fastcall Hooked_IngameCameraUpdate(uintptr_t thisPtr) {
    __int64 ret = 0;
    if (s_origIngameCameraUpdate) {
        ret = s_origIngameCameraUpdate(thisPtr);
    }

    if (Config::bModActive.load(std::memory_order_relaxed) && Config::bFOVActive.load(std::memory_order_relaxed)) {
        HandleFOVHookCallback(thisPtr);
    }

    return ret;
}

static __int64 __fastcall Hooked_ExFollowCameraUpdate(uintptr_t thisPtr, float deltaTime) {
    if (Config::bModActive.load(std::memory_order_relaxed) && thisPtr) {
        if (Config::bFOVActive.load(std::memory_order_relaxed)) {
            UpdateFOVSmoothing(deltaTime);
            __try {
                uintptr_t paramPtr = *reinterpret_cast<uintptr_t*>(thisPtr + Config::Offset_ExFollowParamPtr);
                if (paramPtr >= 0x10000 && paramPtr < 0x7fffffffffffULL) {
                    float currentFOV = Config::CurrentFOV.load(std::memory_order_relaxed);
                    float fovRad = currentFOV * 0.017453292519943295f;
                    *reinterpret_cast<float*>(paramPtr + Config::Offset_ExFollowFOV) = fovRad;
                    *reinterpret_cast<float*>(thisPtr + Config::Offset_LiveFOV) = fovRad;
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {}
        }
    }

    __int64 ret = 0;
    if (s_origExFollowCameraUpdate) {
        ret = s_origExFollowCameraUpdate(thisPtr, deltaTime);
    }

    if (Config::bModActive.load(std::memory_order_relaxed) && thisPtr) {
        if (Config::bFOVActive.load(std::memory_order_relaxed)) {
            __try {
                float currentFOV = Config::CurrentFOV.load(std::memory_order_relaxed);
                float fovRad = currentFOV * 0.017453292519943295f;
                *reinterpret_cast<float*>(thisPtr + Config::Offset_LiveFOV) = fovRad;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {}
        }
    }

    return ret;
}

static __int64 __fastcall Hooked_BuildProjMatrix(uintptr_t outMatrix, float fov, float aspect, float nearClip, float farClip) {
    if (Config::bModActive.load(std::memory_order_relaxed) && Config::bFOVActive.load(std::memory_order_relaxed)) {
        fov = Config::CurrentFOV.load(std::memory_order_relaxed) * 0.017453292519943295f;
    }
    if (s_origBuildProjMatrix) {
        return s_origBuildProjMatrix(outMatrix, fov, aspect, nearClip, farClip);
    }
    return 0;
}

static __int64 __fastcall Hooked_PlayerCameraUpdate(uintptr_t thisPtr, float deltaTime) {
    __int64 ret = 0;
    if (s_origPlayerCameraUpdate) {
        ret = s_origPlayerCameraUpdate(thisPtr, deltaTime);
    }

    if (Config::bModActive.load(std::memory_order_relaxed) && thisPtr) {
        __try {
            if (Config::bFOVActive.load(std::memory_order_relaxed)) {
                float currentFOV = Config::CurrentFOV.load(std::memory_order_relaxed);
                float fovRadians = currentFOV * 0.017453292519943295f;
                *reinterpret_cast<float*>(thisPtr + Config::Offset_LiveFOV) = fovRadians;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    return ret;
}

static __int64 __fastcall Hooked_NormalCameraUpdate(uintptr_t thisPtr, float deltaTime) {
    if (Config::bModActive.load(std::memory_order_relaxed) && thisPtr) {
        __try {
            uintptr_t paramPtr = *reinterpret_cast<uintptr_t*>(thisPtr + Config::Offset_CameraParamPtr);
            if (paramPtr >= 0x10000 && paramPtr < 0x7fffffffffffULL) {
                if (Config::bFOVActive.load(std::memory_order_relaxed)) {
                    float currentFOV = Config::CurrentFOV.load(std::memory_order_relaxed);
                    float fovRadians = currentFOV * 0.017453292519943295f;
                    *reinterpret_cast<float*>(paramPtr + Config::Offset_ParamFOV) = fovRadians;
                    *reinterpret_cast<float*>(thisPtr + Config::Offset_LiveFOV) = fovRadians;
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    __int64 ret = 0;
    if (s_origNormalCameraUpdate) {
        ret = s_origNormalCameraUpdate(thisPtr, deltaTime);
    }

    if (Config::bModActive.load(std::memory_order_relaxed) && thisPtr) {
        __try {
            if (Config::bFOVActive.load(std::memory_order_relaxed)) {
                float currentFOV = Config::CurrentFOV.load(std::memory_order_relaxed);
                float fovRadians = currentFOV * 0.017453292519943295f;
                *reinterpret_cast<float*>(thisPtr + Config::Offset_LiveFOV) = fovRadians;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    return ret;
}

bool ResolveGameManager() {
    if (Config::GlobalGameManagerPtr && IsValidAddress(reinterpret_cast<const void*>(Config::GlobalGameManagerPtr))) {
        return true;
    }

    static DWORD s_lastScanTick = 0;
    DWORD currentTick = GetTickCount();
    if (currentTick - s_lastScanTick < 500) {
        return false;
    }
    s_lastScanTick = currentTick;

    uintptr_t aobInit = FindPattern("DarkSoulsII.exe", Config::SigGameManagerInit);
    if (aobInit) {
        for (uintptr_t p = aobInit - 1; p >= aobInit - 64; --p) {
            if (*reinterpret_cast<uint8_t*>(p) == 0x48 &&
                *reinterpret_cast<uint8_t*>(p + 1) == 0x8B &&
                (*reinterpret_cast<uint8_t*>(p + 2) & 0xC7) == 0x05) {
                int32_t disp = *reinterpret_cast<int32_t*>(p + 3);
                Config::GlobalGameManagerPtr = (p + 7) + disp;
                std::cout << "fov: resolved GlobalGameManager base pointer @ 0x"
                          << std::hex << Config::GlobalGameManagerPtr << std::dec << "\n";
                return true;
            }
        }
    }
    return false;
}

bool InstallCameraHooks() {
    if (s_hooksInstalled) return true;

    uintptr_t imageBase = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
    if (!imageBase) {
        std::cout << "fov: failed to get game module base\n";
        return false;
    }

    // Hook PlayerCameraOperator::Update (Slot 1, RVA 0x00497130)
    uintptr_t playerCamAddr = imageBase + Config::RvaPlayerCameraUpdate;
    if (IsValidAddress(reinterpret_cast<const void*>(playerCamAddr))) {
        if (InstallDetour(playerCamAddr, reinterpret_cast<void*>(&Hooked_PlayerCameraUpdate),
                          reinterpret_cast<void**>(&s_origPlayerCameraUpdate), g_hookPlayerCam)) {
            std::cout << "fov: hooked PlayerCameraOperator::Update @ 0x" << std::hex
                      << playerCamAddr << " -> codecave @ 0x" << g_hookPlayerCam.codecaveAddr << std::dec << "\n";
        }
    }

    // Hook NormalCameraOperator::Update (Slot 2 Sub 1, RVA 0x004A2700)
    uintptr_t normalCamAddr = imageBase + Config::RvaNormalCameraUpdate;
    if (IsValidAddress(reinterpret_cast<const void*>(normalCamAddr))) {
        if (InstallDetour(normalCamAddr, reinterpret_cast<void*>(&Hooked_NormalCameraUpdate),
                          reinterpret_cast<void**>(&s_origNormalCameraUpdate), g_hookNormalCam)) {
            std::cout << "fov: hooked NormalCameraOperator::Update @ 0x" << std::hex
                      << normalCamAddr << " -> codecave @ 0x" << g_hookNormalCam.codecaveAddr << std::dec << "\n";
        }
    }

    // Hook IngameCameraOperator::Update (Slot 2 Container, RVA 0x00495B60)
    uintptr_t ingameCamAddr = imageBase + Config::RvaIngameCameraUpdate;
    if (IsValidAddress(reinterpret_cast<const void*>(ingameCamAddr))) {
        if (InstallDetour(ingameCamAddr, reinterpret_cast<void*>(&Hooked_IngameCameraUpdate),
                          reinterpret_cast<void**>(&s_origIngameCameraUpdate), g_hookIngameCam)) {
            std::cout << "fov: hooked IngameCameraOperator::Update @ 0x" << std::hex
                      << ingameCamAddr << " -> codecave @ 0x" << g_hookIngameCam.codecaveAddr << std::dec << "\n";
        }
    }

    // Hook ExFollowCameraOperator::Update (Slot 2 Sub 5, RVA 0x0049AFB0)
    uintptr_t exFollowAddr = imageBase + Config::RvaExFollowCameraUpdate;
    if (IsValidAddress(reinterpret_cast<const void*>(exFollowAddr))) {
        if (InstallDetour(exFollowAddr, reinterpret_cast<void*>(&Hooked_ExFollowCameraUpdate),
                          reinterpret_cast<void**>(&s_origExFollowCameraUpdate), g_hookExFollowCam)) {
            std::cout << "fov: hooked ExFollowCameraOperator::Update @ 0x" << std::hex
                      << exFollowAddr << " -> codecave @ 0x" << g_hookExFollowCam.codecaveAddr << std::dec << "\n";
        }
    }

    // Hook BuildProjMatrix (RVA 0x00001A90)
    uintptr_t projAddr = imageBase + Config::RvaBuildProjMatrix;
    if (IsValidAddress(reinterpret_cast<const void*>(projAddr))) {
        if (InstallDetour(projAddr, reinterpret_cast<void*>(&Hooked_BuildProjMatrix),
                          reinterpret_cast<void**>(&s_origBuildProjMatrix), g_hookBuildProjMatrix)) {
            std::cout << "fov: hooked BuildProjMatrix @ 0x" << std::hex
                      << projAddr << " -> codecave @ 0x" << g_hookBuildProjMatrix.codecaveAddr << std::dec << "\n";
        }
    }

    Config::AddrCameraHook = ingameCamAddr;
    Config::AddrCameraCodecave = g_hookIngameCam.codecaveAddr;
    Config::HookLen = g_hookIngameCam.hookLen;

    s_hooksInstalled = g_hookExFollowCam.isInstalled || g_hookPlayerCam.isInstalled ||
                       g_hookNormalCam.isInstalled || g_hookIngameCam.isInstalled ||
                       g_hookBuildProjMatrix.isInstalled;
    return s_hooksInstalled;
}

void UninstallCameraHooks() {
    UninstallDetour(g_hookBuildProjMatrix);
    UninstallDetour(g_hookExFollowCam);
    UninstallDetour(g_hookPlayerCam);
    UninstallDetour(g_hookNormalCam);
    UninstallDetour(g_hookIngameCam);
    s_hooksInstalled = false;
}

void ApplyFOVHooks() {
    std::cout << "fov: activating dynamic FOV hooks\n";
    if (InstallCameraHooks()) {
        Config::bFOVActive.store(true);
        std::cout << "fov: FOV hooks active\n";
    } else {
        std::cout << "fov: waiting for camera hook targets\n";
    }
}

void SetFOVHooksEnabled(bool enable) {
    Config::bFOVActive.store(enable);
    std::cout << "fov: FOV feature " << (enable ? "enabled" : "disabled") << "\n";
}

void HandleFOVHookCallback(uintptr_t camera) {
    if (!camera || !IsValidAddress(reinterpret_cast<const void*>(camera))) return;
    Config::GlobalCameraPtr = camera;

    const float currentFOV = Config::CurrentFOV.load(std::memory_order_relaxed);
    const float fovRadians = currentFOV * 0.017453292519943295f;

    // Update ExFollowCameraOperator
    uintptr_t exFollow = camera + Config::Offset_IngameExFollowCam;
    if (IsValidAddress(reinterpret_cast<const void*>(exFollow))) {
        __try {
            *reinterpret_cast<float*>(exFollow + Config::Offset_LiveFOV) = fovRadians;
            uintptr_t paramPtr = *reinterpret_cast<uintptr_t*>(exFollow + Config::Offset_ExFollowParamPtr);
            if (paramPtr >= 0x10000 && paramPtr < 0x7fffffffffffULL) {
                *reinterpret_cast<float*>(paramPtr + Config::Offset_ExFollowFOV) = fovRadians;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    // Update sub-operators
    for (uint32_t offset : s_CameraSubOffsets) {
        uintptr_t camOp = camera + offset;
        if (IsValidAddress(reinterpret_cast<const void*>(camOp))) {
            __try {
                *reinterpret_cast<float*>(camOp + Config::Offset_LiveFOV) = fovRadians;

                uintptr_t paramPtr = *reinterpret_cast<uintptr_t*>(camOp + Config::Offset_CameraParamPtr);
                if (paramPtr >= 0x10000 && paramPtr < 0x7fffffffffffULL) {
                    *reinterpret_cast<float*>(paramPtr + Config::Offset_ParamFOV) = fovRadians;
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {}
        }
    }
}
