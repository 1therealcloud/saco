#pragma once

#include <stdafx.h>

#include <type_traits>

namespace patch
{
    inline const std::vector<uint8_t> RET = {0xC3};
    inline const std::vector<uint8_t> RET_ZERO = {0x33, 0xC0, 0xC3};
    inline const std::vector<uint8_t> JMP_SHORT = {0xEB};

    inline bool write(uintptr_t address, const void* data, size_t size)
    {
        if (!address || (!data && size != 0))
            return false;

        if (size == 0)
            return true;

        auto* target = reinterpret_cast<void*>(address);
        DWORD oldProtect{};

        if (!VirtualProtect(target, size, PAGE_EXECUTE_READWRITE, &oldProtect))
            return false;

        std::memcpy(target, data, size);

        DWORD unusedProtect{};
        const bool protectionRestored = VirtualProtect(target, size, oldProtect, &unusedProtect) != FALSE;

        FlushInstructionCache(GetCurrentProcess(), target, size);

        if (!protectionRestored)
            return false;

        return true;
    }

    inline bool write(uintptr_t address, const std::vector<uint8_t>& bytes)
    {
        return write(address, bytes.data(), bytes.size());
    }

    template <typename T>
    inline bool write(uintptr_t address, const T& value)
    {
        return write(address, &value, sizeof(value));
    }

    inline bool read(uintptr_t address, void* data, size_t size)
    {
        if (!address || (!data && size != 0))
            return false;

        if (size == 0)
            return true;

        SIZE_T bytesRead{};
        return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address), data, size, &bytesRead) != FALSE && bytesRead == size;
    }

    inline bool expect(uintptr_t address, const void* expected, size_t size)
    {
        if (!address || (!expected && size != 0))
            return false;

        if (size == 0)
            return true;

        std::vector<uint8_t> actual(size);
        if (!read(address, actual.data(), actual.size()))
            return false;

        return std::memcmp(actual.data(), expected, size) == 0;
    }

    template <typename T>
    inline bool expect(uintptr_t address, const T& expected)
    {
        return expect(address, &expected, sizeof(expected));
    }

    template <typename T>
    inline bool replace(uintptr_t address, const T& expected, const T& replacement)
    {
        if (!expect(address, expected))
            return false;

        return write(address, replacement);
    }

    inline bool fill(uintptr_t address, uint8_t value, size_t size)
    {
        return write(address, std::vector<uint8_t>(size, value));
    }

    inline bool nop(uintptr_t address, size_t size)
    {
        return fill(address, 0x90, size);
    }

    inline bool relative_branch(uintptr_t address, const void* destination, uint8_t opcode, size_t size)
    {
        constexpr size_t kInstructionSize = 5;

        if (!address || !destination || size < kInstructionSize)
            return false;

        const auto target = reinterpret_cast<uintptr_t>(destination);
        const auto next = address + kInstructionSize;
        const auto delta = static_cast<std::intptr_t>(target) - static_cast<std::intptr_t>(next);

        if (delta < std::numeric_limits<std::int32_t>::min() || delta > std::numeric_limits<std::int32_t>::max())
            return false;

        std::vector<uint8_t> bytes(size, 0x90);
        bytes[0] = opcode;

        const auto relative = static_cast<std::int32_t>(delta);
        std::memcpy(bytes.data() + 1, &relative, sizeof(relative));

        return write(address, bytes);
    }

    inline bool call(uintptr_t address, const void* destination)
    {
        return relative_branch(address, destination, 0xE8, 5);
    }

    inline bool jump(uintptr_t address, const void* destination, size_t size = 5)
    {
        return relative_branch(address, destination, 0xE9, size);
    }

    inline bool expect_call(uintptr_t address, uintptr_t destination)
    {
        uint8_t opcode{};
        std::int32_t relative{};

        if (!read(address, &opcode, sizeof(opcode)) || opcode != 0xE8)
            return false;

        if (!read(address + 1, &relative, sizeof(relative)))
            return false;

        const auto actual = static_cast<uintptr_t>(static_cast<std::intptr_t>(address + 5) + static_cast<std::intptr_t>(relative));

        return actual == destination;
    }

    struct Entry
    {
        uintptr_t address;
        std::vector<uint8_t> bytes;
    };

    template <size_t N>
    bool apply(const Entry (&entries)[N])
    {
        for (const auto& e : entries)
        {
            if (!write(e.address, e.bytes))
                return false;
        }

        return true;
    }
}
