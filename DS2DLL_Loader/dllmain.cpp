#include <windows.h>
#include "proxy_dinput8.h"
#include "proxy_winmm.h"
#include "proxy_dxgi.h"
#include "proxy_d3d11.h"
#include "loader.h"

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved) {
    switch (reason) {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        Loader::Initialize(hModule);
        break;
    case DLL_PROCESS_DETACH:
        Loader::Cleanup();
        break;
    }
    return TRUE;
}
