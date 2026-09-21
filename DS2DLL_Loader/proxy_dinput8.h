#pragma once

#pragma comment(linker, "/export:DirectInput8Create=C:\\Windows\\System32\\dinput8.DirectInput8Create")
#pragma comment(linker, "/export:DllCanUnloadNow=C:\\Windows\\System32\\dinput8.DllCanUnloadNow,PRIVATE")
#pragma comment(linker, "/export:DllGetClassObject=C:\\Windows\\System32\\dinput8.DllGetClassObject,PRIVATE")
#pragma comment(linker, "/export:DllRegisterServer=C:\\Windows\\System32\\dinput8.DllRegisterServer,PRIVATE")
#pragma comment(linker, "/export:DllUnregisterServer=C:\\Windows\\System32\\dinput8.DllUnregisterServer,PRIVATE")
#pragma comment(linker, "/export:GetdfDIJoystick=C:\\Windows\\System32\\dinput8.GetdfDIJoystick")
