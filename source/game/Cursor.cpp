/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * The original code is copyright (c) 2026, 1therealcloud.
 */

#include <stdafx.h>

#include "Cursor.h"

#include "CPad.h"
#include "sync/KeyStuff.h"

namespace
{
    constexpr std::uintptr_t kReadKeysCall = 0x541DF5;
    constexpr std::uintptr_t kReadMouseCall = 0x53F417;
    constexpr std::uintptr_t kMouseResultBranch = 0x53F41F;
    constexpr std::uintptr_t kSetMousePosition = 0x6194A0;
    constexpr std::uintptr_t kUpdateMouse = 0x53F3C0;
    constexpr std::uintptr_t kMouseStates = 0xB73404;
    constexpr std::size_t kMouseStatesSize = 3 * 0x14;
    constexpr std::uintptr_t kPadStates = 0xB73458;
    constexpr std::size_t kPadStatesSize = 48 * sizeof(WORD);
}

namespace game::cursor
{
    void SetMode(int mode)
    {
        ZoneScoped;
        if (mode < CURSOR_NONE || mode > CURSOR_LOCKCAM_NOCURSOR)
            return;

        const bool blockKeys = mode == CURSOR_LOCKKEYS_NOCURSOR || mode == CURSOR_LOCKCAMANDCONTROL;
        const bool blockMouse = mode == CURSOR_LOCKCAMANDCONTROL || mode == CURSOR_LOCKCAM || mode == CURSOR_LOCKCAM_NOCURSOR;

        constexpr std::array<std::uint8_t, 5> readKeys{0xE8, 0x46, 0xF3, 0xFE, 0xFF};
        constexpr std::array<std::uint8_t, 5> disabledInput{0x90, 0x90, 0x90, 0x90, 0x90};
        constexpr std::uint8_t ret = 0xC3;
        static std::array<std::uint8_t, 5> readMouse{};
        static std::array<std::uint8_t, 4> mouseResultBranch{};
        constexpr std::array<std::uint8_t, 4> skipMouseResult{0x33, 0xC0, 0x0F, 0x84};
        static std::uint8_t setMousePos = 0;
        static bool initialized = false;
        static bool keysBlocked = false;
        static bool mouseBlocked = false;

        if (!initialized)
        {
            if (!patch::read(kReadMouseCall, readMouse.data(), readMouse.size()) ||
                !patch::read(kMouseResultBranch, mouseResultBranch.data(), mouseResultBranch.size()) ||
                !patch::read(kSetMousePosition, &setMousePos, sizeof(setMousePos)))
            {
#if defined(_DEBUG)
                Log("failed to save game mouse instructions!");
#endif // _DEBUG
                ExitProcess(1);
            }
        }

        const bool releasingMouse = !blockMouse && mouseBlocked;
        if (!initialized || blockMouse != mouseBlocked)
        {
            const auto& mouseBytes = blockMouse ? disabledInput : readMouse;
            const auto& resultBytes = blockMouse ? skipMouseResult : mouseResultBranch;
            if (!patch::write(kReadMouseCall, mouseBytes.data(), mouseBytes.size()) ||
                !patch::write(kMouseResultBranch, resultBytes.data(), resultBytes.size()) ||
                !patch::write<std::uint8_t>(kSetMousePosition, blockMouse ? ret : setMousePos))
            {
#if defined(_DEBUG)
                Log("failed to change game mouse state!");
#endif // _DEBUG
                ExitProcess(1);
            }
            mouseBlocked = blockMouse;
        }

        if (releasingMouse)
        {
            // read pending movement only after restoring the native polling path.
            using UpdateMouseFn = void(__thiscall*)(CPad*);
            reinterpret_cast<UpdateMouseFn>(kUpdateMouse)(KeyStuff::GetInternalKeys());
        }

        std::memset(reinterpret_cast<void*>(kMouseStates), 0, kMouseStatesSize);

        if (!initialized || blockKeys != keysBlocked)
        {
            const auto& keyBytes = blockKeys ? disabledInput : readKeys;
            if (!patch::write(kReadKeysCall, keyBytes.data(), keyBytes.size()))
            {
#if defined(_DEBUG)
                Log("failed to change game input state!");
#endif // _DEBUG
                ExitProcess(1);
            }
            keysBlocked = blockKeys;
        }
        initialized = true;

        std::memset(reinterpret_cast<void*>(kPadStates), 0, kPadStatesSize);
    }
}
