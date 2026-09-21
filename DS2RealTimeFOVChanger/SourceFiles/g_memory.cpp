#include "g_memory.h"
#include <string>
#include <sstream>
#include <cstring>
#include <algorithm>

void ParseSignature(const char* sig, std::vector<int>& bytes) {
    std::stringstream ss(sig);
    std::string t;
    while (ss >> t) bytes.push_back((t == "??" || t == "?") ? -1 : std::stoul(t, nullptr, 16));
}

uintptr_t FindPattern(const char* mod, const char* sig) {
    HMODULE hMod = GetModuleHandleA(mod);
    if (!hMod) return 0;

    PIMAGE_DOS_HEADER dH = (PIMAGE_DOS_HEADER)hMod;
    PIMAGE_NT_HEADERS ntH = (PIMAGE_NT_HEADERS)((uint8_t*)hMod + dH->e_lfanew);
    DWORD imgSize = ntH->OptionalHeader.SizeOfImage;

    std::vector<int> pat;
    ParseSignature(sig, pat);
    uint8_t* scan = (uint8_t*)hMod;

    for (DWORD i = 0; i < imgSize - pat.size(); ++i) {
        bool found = true;
        for (size_t j = 0; j < pat.size(); ++j) {
            if (pat[j] != -1 && scan[i + j] != pat[j]) { found = false; break; }
        }
        if (found) return (uintptr_t)&scan[i];
    }
    return 0;
}

void* AllocateNearAddress(uintptr_t target, size_t size) {
    SYSTEM_INFO sI;
    GetSystemInfo(&sI);
    uint64_t pSize = sI.dwPageSize;

    uint64_t start = (target & ~(pSize - 1));
    uint64_t minA = (std::max)(start > 0x7FFFFF00 ? start - 0x7FFFFF00 : (uint64_t)sI.lpMinimumApplicationAddress, (uint64_t)sI.lpMinimumApplicationAddress);
    uint64_t maxA = (std::min)(start + 0x7FFFFF00, (uint64_t)sI.lpMaximumApplicationAddress);

    for (uint64_t a = start; a > minA; a -= pSize) {
        if (void* alloc = VirtualAlloc((void*)a, size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE)) return alloc;
    }
    for (uint64_t a = start; a < maxA; a += pSize) {
        if (void* alloc = VirtualAlloc((void*)a, size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE)) return alloc;
    }
    return nullptr;
}

uintptr_t FindString(const char* moduleName, const char* str) {
    HMODULE hMod = GetModuleHandleA(moduleName);
    if (!hMod || !str) return 0;

    PIMAGE_DOS_HEADER dH = (PIMAGE_DOS_HEADER)hMod;
    PIMAGE_NT_HEADERS ntH = (PIMAGE_NT_HEADERS)((uint8_t*)hMod + dH->e_lfanew);
    DWORD imgSize = ntH->OptionalHeader.SizeOfImage;

    size_t strLen = strlen(str) + 1;
    uint8_t* scan = (uint8_t*)hMod;

    for (DWORD i = 0; i < imgSize - strLen; ++i) {
        if (memcmp(&scan[i], str, strLen) == 0) {
            return (uintptr_t)&scan[i];
        }
    }
    return 0;
}

uintptr_t FindRipRef(const char* moduleName, uintptr_t targetAddr, uint8_t regOpcode) {
    HMODULE hMod = GetModuleHandleA(moduleName);
    if (!hMod || !targetAddr) return 0;

    PIMAGE_DOS_HEADER dH = (PIMAGE_DOS_HEADER)hMod;
    PIMAGE_NT_HEADERS ntH = (PIMAGE_NT_HEADERS)((uint8_t*)hMod + dH->e_lfanew);
    DWORD imgSize = ntH->OptionalHeader.SizeOfImage;

    uint8_t* scan = (uint8_t*)hMod;

    for (DWORD i = 0; i < imgSize - 7; ++i) {
        // lea reg, [rip+disp]
        if (scan[i] == 0x48 && scan[i + 1] == 0x8D && scan[i + 2] == regOpcode) {
            int32_t disp = *reinterpret_cast<int32_t*>(&scan[i + 3]);
            uintptr_t ripAfter = (uintptr_t)&scan[i + 7];
            if ((ripAfter + disp) == targetAddr) {
                return (uintptr_t)&scan[i];
            }
        }
    }
    return 0;
}

uintptr_t ResolveCallTarget(uintptr_t callInstructionAddr) {
    if (!callInstructionAddr || *reinterpret_cast<uint8_t*>(callInstructionAddr) != 0xE8) {
        return 0;
    }
    int32_t disp = *reinterpret_cast<int32_t*>(callInstructionAddr + 1);
    return (callInstructionAddr + 5) + disp;
}

uintptr_t FindNthCallForward(uintptr_t startAddr, size_t maxScanBytes, size_t targetCallIndex) {
    if (!startAddr) return 0;

    uint8_t* code = reinterpret_cast<uint8_t*>(startAddr);
    size_t callCount = 0;

    for (size_t i = 0; i < maxScanBytes; ++i) {
        if (code[i] == 0xE8) {
            ++callCount;
            if (callCount == targetCallIndex) {
                return reinterpret_cast<uintptr_t>(&code[i]);
            }
        }
    }
    return 0;
}

const char* GetRttiName(uintptr_t obj) {
    if (!obj) return "(null)";
    __try {
        uintptr_t vtable = *reinterpret_cast<uintptr_t*>(obj);
        if (vtable < 0x7ff000000000ULL || vtable > 0x7fffffffffffULL) return "(bad vtable)";
        uintptr_t colPtr = *reinterpret_cast<uintptr_t*>(vtable - 8);
        if (colPtr < 0x7ff000000000ULL || colPtr > 0x7fffffffffffULL) return "(bad colPtr)";

        uint32_t* col = reinterpret_cast<uint32_t*>(colPtr);
        uint32_t selfRva = col[5];
        if (!selfRva) return "(no selfRva)";
        uintptr_t imageBase = colPtr - selfRva;
        uint32_t typeDescRva = col[3];
        uintptr_t typeDesc = imageBase + typeDescRva;
        const char* name = reinterpret_cast<const char*>(typeDesc + 0x10);
        if (name && name[0] == '.') {
            return name;
        }
        return "(no rtti name)";
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return "(exception)";
    }
}

size_t GetInstructionLength(const uint8_t* code) {
    if (!code) return 0;
    const uint8_t* p = code;
    bool hasOperandPrefix = false;
    bool hasRexW = false;

    while (true) {
        uint8_t b = *p;
        if (b == 0x66) { hasOperandPrefix = true; ++p; }
        else if (b == 0x67) { ++p; }
        else if (b == 0xF0 || b == 0xF2 || b == 0xF3 ||
                 b == 0x2E || b == 0x36 || b == 0x3E || b == 0x26 || b == 0x64 || b == 0x65) {
            ++p;
        }
        else if (b >= 0x40 && b <= 0x4F) {
            if (b & 0x08) hasRexW = true;
            ++p;
        }
        else {
            break;
        }
        if (p - code >= 15) return 15;
    }

    uint8_t op = *p++;
    bool hasModRm = false;
    int immSize = 0;

    if (op == 0x0F) {
        op = *p++;
        if (op == 0x38 || op == 0x3A) {
            ++p;
            hasModRm = true;
            if (op == 0x3A) immSize = 1;
        } else {
            if ((op >= 0x10 && op <= 0x17) || (op >= 0x20 && op <= 0x23) ||
                (op >= 0x28 && op <= 0x2F) || (op >= 0x40 && op <= 0x6F) ||
                (op >= 0x70 && op <= 0x7F) || (op >= 0x90 && op <= 0x9F) ||
                (op >= 0xA3 && op <= 0xAF && op != 0xA5 && op != 0xAD) ||
                (op >= 0xB2 && op <= 0xBF) || (op >= 0xC2 && op <= 0xC6) ||
                (op >= 0xD0 && op <= 0xFE)) {
                hasModRm = true;
            }
            if (op >= 0x80 && op <= 0x8F) {
                immSize = 4;
            } else if (op == 0x70 || op == 0xC2 || op == 0xC4 || op == 0xC5 || op == 0xC6 || op == 0xBA) {
                immSize = 1;
            }
        }
    } else {
        switch (op) {
            case 0x00: case 0x01: case 0x02: case 0x03:
            case 0x08: case 0x09: case 0x0A: case 0x0B:
            case 0x10: case 0x11: case 0x12: case 0x13:
            case 0x18: case 0x19: case 0x1A: case 0x1B:
            case 0x20: case 0x21: case 0x22: case 0x23:
            case 0x28: case 0x29: case 0x2A: case 0x2B:
            case 0x30: case 0x31: case 0x32: case 0x33:
            case 0x38: case 0x39: case 0x3A: case 0x3B:
            case 0x63: case 0x69: case 0x6B:
            case 0x80: case 0x81: case 0x82: case 0x83:
            case 0x84: case 0x85: case 0x86: case 0x87:
            case 0x88: case 0x89: case 0x8A: case 0x8B:
            case 0x8C: case 0x8D: case 0x8E: case 0x8F:
            case 0xC0: case 0xC1: case 0xC6: case 0xC7:
            case 0xD0: case 0xD1: case 0xD2: case 0xD3:
            case 0xF6: case 0xF7: case 0xFE: case 0xFF:
                hasModRm = true;
                break;
        }

        switch (op) {
            case 0x04: case 0x0C: case 0x14: case 0x1C:
            case 0x24: case 0x2C: case 0x34: case 0x3C:
            case 0x6A: case 0x70: case 0x71: case 0x72:
            case 0x73: case 0x74: case 0x75: case 0x76:
            case 0x77: case 0x78: case 0x79: case 0x7A:
            case 0x7B: case 0x7C: case 0x7D: case 0x7E:
            case 0x7F: case 0x80: case 0x82: case 0x83:
            case 0x6B: case 0xA8: case 0xB0: case 0xB1:
            case 0xB2: case 0xB3: case 0xB4: case 0xB5:
            case 0xB6: case 0xB7: case 0xC0: case 0xC1:
            case 0xC6: case 0xCD: case 0xD4: case 0xD5:
            case 0xE4: case 0xE5: case 0xEB:
                immSize = 1;
                break;
            case 0xC2: case 0xCA:
                immSize = 2;
                break;
            case 0x05: case 0x0D: case 0x15: case 0x1D:
            case 0x25: case 0x2D: case 0x35: case 0x3D:
            case 0x68: case 0x69: case 0x81: case 0xA9:
            case 0xC7: case 0xE8: case 0xE9:
                immSize = hasOperandPrefix ? 2 : 4;
                break;
            case 0xB8: case 0xB9: case 0xBA: case 0xBB:
            case 0xBC: case 0xBD: case 0xBE: case 0xBF:
                immSize = hasRexW ? 8 : (hasOperandPrefix ? 2 : 4);
                break;
        }
    }

    if (hasModRm) {
        uint8_t modrm = *p++;
        uint8_t mod = (modrm >> 6) & 3;
        uint8_t reg = (modrm >> 3) & 7;
        uint8_t rm  = modrm & 7;

        if (op == 0xF6 && reg == 0) immSize = 1;
        if (op == 0xF7 && reg == 0) immSize = hasOperandPrefix ? 2 : 4;

        if (mod != 3 && rm == 4) {
            uint8_t sib = *p++;
            uint8_t base = sib & 7;
            if (base == 5 && mod == 0) {
                p += 4;
            }
        }

        if (mod == 1) {
            p += 1;
        } else if (mod == 2) {
            p += 4;
        } else if (mod == 0 && rm == 5) {
            p += 4; 
        }
    }

    p += immSize;
    size_t len = p - code;
    return (len > 0 && len <= 15) ? len : 1;
}

size_t CalculateHookLength(uintptr_t address, size_t minLength) {
    if (!address) return minLength;
    size_t total = 0;
    while (total < minLength && total < 32) {
        size_t len = GetInstructionLength(reinterpret_cast<const uint8_t*>(address + total));
        if (len == 0 || len > 15) {
            break;
        }
        total += len;
    }
    return (total >= minLength) ? total : minLength;
}

bool IsValidAddress(const void* address) {
    if (!address) return false;
    uintptr_t addr = reinterpret_cast<uintptr_t>(address);
    if (addr < 0x10000 || addr >= 0x7fffffffffffULL) return false;

    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery(address, &mbi, sizeof(mbi))) return false;
    return (mbi.State == MEM_COMMIT) &&
           !(mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) &&
           (mbi.Protect & (PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_READWRITE | PAGE_READONLY));
}

bool WriteMemoryFloat(uintptr_t address, float value) {
    if (!address || !IsValidAddress(reinterpret_cast<const void*>(address))) return false;

    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery(reinterpret_cast<void*>(address), &mbi, sizeof(mbi))) return false;
    if (mbi.State != MEM_COMMIT) return false;
    if (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) return false;

    if (mbi.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE)) {
        *reinterpret_cast<float*>(address) = value;
        return true;
    }
    else if (mbi.Protect & (PAGE_READONLY | PAGE_EXECUTE_READ)) {
        DWORD oldProtect;
        if (VirtualProtect(reinterpret_cast<void*>(address), sizeof(float), PAGE_EXECUTE_READWRITE, &oldProtect)) {
            *reinterpret_cast<float*>(address) = value;
            VirtualProtect(reinterpret_cast<void*>(address), sizeof(float), oldProtect, &oldProtect);
            return true;
        }
    }
    return false;
}

bool InstallDetour(uintptr_t targetAddr, void* hookFunction, void** outOriginalTrampoline, HookRecord& record) {
    if (!targetAddr || !hookFunction || !IsValidAddress(reinterpret_cast<const void*>(targetAddr))) {
        return false;
    }

    size_t hookLen = CalculateHookLength(targetAddr, 5);
    if (hookLen < 5 || hookLen > 24) {
        return false;
    }

    record.targetAddr = targetAddr;
    record.hookLen = hookLen;
    memcpy(record.origBytes, reinterpret_cast<const void*>(targetAddr), hookLen);

    uint8_t* codecave = reinterpret_cast<uint8_t*>(AllocateNearAddress(targetAddr, 256));
    if (!codecave) {
        return false;
    }
    record.codecaveAddr = reinterpret_cast<uintptr_t>(codecave);


    codecave[0] = 0xFF;
    codecave[1] = 0x25;
    *reinterpret_cast<int32_t*>(&codecave[2]) = 0;
    *reinterpret_cast<uint64_t*>(&codecave[6]) = reinterpret_cast<uint64_t>(hookFunction);

    uint8_t* pTrampoline = codecave + 32;
    memcpy(pTrampoline, record.origBytes, hookLen);
    pTrampoline[hookLen + 0] = 0xFF;
    pTrampoline[hookLen + 1] = 0x25;
    *reinterpret_cast<int32_t*>(&pTrampoline[hookLen + 2]) = 0;
    *reinterpret_cast<uint64_t*>(&pTrampoline[hookLen + 6]) = targetAddr + hookLen;

    if (outOriginalTrampoline) {
        *outOriginalTrampoline = pTrampoline;
    }

    FlushInstructionCache(GetCurrentProcess(), codecave, 256);

    uint8_t patch[32] = { 0 };
    patch[0] = 0xE9;
    *reinterpret_cast<int32_t*>(&patch[1]) = static_cast<int32_t>(
        reinterpret_cast<uintptr_t>(codecave) - (targetAddr + 5)
    );
    for (size_t i = 5; i < hookLen; ++i) {
        patch[i] = 0x90; 
    }

    DWORD oldProtect;
    if (!VirtualProtect(reinterpret_cast<void*>(targetAddr), hookLen, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        return false;
    }

    memcpy(reinterpret_cast<void*>(targetAddr), patch, hookLen);
    VirtualProtect(reinterpret_cast<void*>(targetAddr), hookLen, oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(targetAddr), hookLen);

    record.isInstalled = true;
    return true;
}

void UninstallDetour(HookRecord& record) {
    if (!record.isInstalled || !record.targetAddr || record.hookLen == 0) return;

    DWORD oldProtect;
    if (VirtualProtect(reinterpret_cast<void*>(record.targetAddr), record.hookLen, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        memcpy(reinterpret_cast<void*>(record.targetAddr), record.origBytes, record.hookLen);
        VirtualProtect(reinterpret_cast<void*>(record.targetAddr), record.hookLen, oldProtect, &oldProtect);
        FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(record.targetAddr), record.hookLen);
    }

    if (record.codecaveAddr) {
        VirtualFree(reinterpret_cast<void*>(record.codecaveAddr), 0, MEM_RELEASE);
        record.codecaveAddr = 0;
    }

    record.isInstalled = false;
}

