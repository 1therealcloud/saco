/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * The original code is copyright (c) 2026, 1therealcloud.
 */

#include <stdafx.h>
#include "main.h"

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
                // ExitProcess stops worker threads before CRT global destructors.
                // The OS reclaims resources; native cleanup here can touch destroyed
                // model registries or wait for download workers that cannot run.
                g_netGame.release();
                g_game.release();
                break;
            }

            break;
    }

    return TRUE;
}
