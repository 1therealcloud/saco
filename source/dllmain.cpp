#include <stdafx.h>

extern DWORD WINAPI InitializeThread(LPVOID);

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    switch (fdwReason)
    {
        case DLL_PROCESS_ATTACH:
        {
            DisableThreadLibraryCalls(hinstDLL);

            HANDLE initializeThreadHandle = CreateThread(nullptr, 0, InitializeThread, nullptr, 0, nullptr);

            // when init failed
            if (!initializeThreadHandle)
                ExitProcess(1);

            CloseHandle(initializeThreadHandle);

            break;
        }

        case DLL_THREAD_ATTACH:
            break;

        case DLL_THREAD_DETACH:
            break;

        case DLL_PROCESS_DETACH:
            if (lpvReserved != nullptr)
            {
                break;
            }

            break;
    }

    return TRUE;
}
