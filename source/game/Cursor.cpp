#include <stdafx.h>

#include "Cursor.h"

#include "CPad.h"
#include "sync/KeyStuff.h"

namespace
{
    constexpr std::uintptr_t kReadKeysCall = 0x541DF5;
    constexpr std::uintptr_t kReadMouseCall = 0x541DD7;
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
        if (mode < CURSOR_NONE || mode > CURSOR_LOCKCAM_NOCURSOR)
            return;

        const bool blockKeys = mode == CURSOR_LOCKKEYS_NOCURSOR || mode == CURSOR_LOCKCAMANDCONTROL;
        const bool blockMouse = mode == CURSOR_LOCKCAMANDCONTROL || mode == CURSOR_LOCKCAM || mode == CURSOR_LOCKCAM_NOCURSOR;

        constexpr std::array<std::uint8_t, 5> readKeys{0xE8, 0x46, 0xF3, 0xFE, 0xFF};
        constexpr std::array<std::uint8_t, 5> disabledInput{0x90, 0x90, 0x90, 0x90, 0x90};
        constexpr std::uint8_t ret = 0xC3;
        static std::array<std::uint8_t, 5> readMouse{};
        static std::uint8_t setMousePos = 0;
        static bool initialized = false;
        static bool keysBlocked = false;
        static bool mouseBlocked = false;

        if (!initialized)
        {
            if (!patch::read(kReadMouseCall, readMouse.data(), readMouse.size()) || !patch::read(kSetMousePosition, &setMousePos, sizeof(setMousePos)))
            {
                Log("failed to save game mouse instructions!");
                ExitProcess(1);
            }
        }

        if (!blockMouse && mouseBlocked)
        {
            // consume movement accumulated while mouse input was disabled
            using UpdateMouseFn = void(__thiscall*)(CPad*);
            reinterpret_cast<UpdateMouseFn>(kUpdateMouse)(KeyStuff::GetInternalKeys());
        }

        if (!initialized || blockMouse != mouseBlocked)
        {
            const auto& mouseBytes = blockMouse ? disabledInput : readMouse;
            if (!patch::write(kReadMouseCall, mouseBytes.data(), mouseBytes.size()) || !patch::write<std::uint8_t>(kSetMousePosition, blockMouse ? ret : setMousePos))
            {
                Log("failed to change game mouse state!");
                ExitProcess(1);
            }
            mouseBlocked = blockMouse;
        }

        std::memset(reinterpret_cast<void*>(kMouseStates), 0, kMouseStatesSize);

        if (!initialized || blockKeys != keysBlocked)
        {
            const auto& keyBytes = blockKeys ? disabledInput : readKeys;
            if (!patch::write(kReadKeysCall, keyBytes.data(), keyBytes.size()))
            {
                Log("failed to change game input state!");
                ExitProcess(1);
            }
            keysBlocked = blockKeys;
        }
        initialized = true;

        std::memset(reinterpret_cast<void*>(kPadStates), 0, kPadStatesSize);
    }
}
