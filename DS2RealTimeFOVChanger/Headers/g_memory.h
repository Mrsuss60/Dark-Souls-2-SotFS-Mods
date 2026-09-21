#pragma once
#include <windows.h>
#include <vector>
#include <cstdint>

void ParseSignature(const char* signature, std::vector<int>& bytes);
uintptr_t FindPattern(const char* moduleName, const char* signature);
void* AllocateNearAddress(uintptr_t targetAddr, size_t size);

uintptr_t FindString(const char* moduleName, const char* str);
uintptr_t FindRipRef(const char* moduleName, uintptr_t targetAddr, uint8_t regOpcode = 0x15);
uintptr_t ResolveCallTarget(uintptr_t callInstructionAddr);
uintptr_t FindNthCallForward(uintptr_t startAddr, size_t maxScanBytes, size_t targetCallIndex);

const char* GetRttiName(uintptr_t obj);

size_t GetInstructionLength(const uint8_t* code);
size_t CalculateHookLength(uintptr_t address, size_t minLength = 5);

bool IsValidAddress(const void* address);
bool WriteMemoryFloat(uintptr_t address, float value);

struct HookRecord {
    uintptr_t targetAddr = 0;
    uintptr_t codecaveAddr = 0;
    size_t hookLen = 0;
    uint8_t origBytes[32] = { 0 };
    bool isInstalled = false;
};

bool InstallDetour(uintptr_t targetAddr, void* hookFunction, void** outOriginalTrampoline, HookRecord& record);
void UninstallDetour(HookRecord& record);
