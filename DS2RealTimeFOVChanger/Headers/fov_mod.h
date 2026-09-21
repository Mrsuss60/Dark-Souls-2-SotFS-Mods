#pragma once
#include <windows.h>
#include <cstdint>

bool ResolveGameManager();
bool InstallCameraHooks();
void UninstallCameraHooks();
void ApplyFOVHooks();
void SetFOVHooksEnabled(bool enable);
void HandleFOVHookCallback(uintptr_t camera);
void UpdateFOVSmoothing(float deltaTime);
