#pragma once
#include <windows.h>
#include <string>

namespace Loader {
    void Initialize(HMODULE hModule);
    void Cleanup();
}
