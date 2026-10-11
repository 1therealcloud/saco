#pragma once

#include <cstddef>
#include <cstdint>

// r5 has no separate playercolors file: these were standalone samp.dll routines
// r5 called initialize from CGame::InitGame; out project keeps them in a separate
// module owned by CNetGame, because the palette belongs to multiplayer, not CGame
namespace PlayerColors
{
    constexpr std::size_t COUNT = 1000;

    void SetColor(std::uint32_t id, std::uint32_t color); // 0xA7210
    std::uint32_t GetColor(std::uint32_t id);             // 0xA7230
    void Initialize();                                    // 0xA7280
}
