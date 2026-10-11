#pragma once

#include <cstddef>

class CPad;

namespace KeyStuff
{
    constexpr std::size_t PLAYER_COUNT = 210;

    CPad& RefLocalPlayerKeys();   // 0x1527C8
    CPad* ArrayPlayerKeys();      // 0x152900
    CPad*& RefInternalKeys();     // 0x114AE8
    bool*& RefDriveByLeft();      // 0x114AEC
    bool*& RefDriveByRight();     // 0x114AF0
    bool& RefSavedDriveByLeft();  // 0x1625A8
    bool& RefSavedDriveByRight(); // 0x1625A9

    void Initialize();                          // 0xA72A0
    void UpdateKeys();                          // 0xA72C0
    void ApplyKeys();                           // 0xA7300
    void SetKeys(int player, const CPad* keys); // 0xA7340
    void ApplyKeys(int player);                 // 0xA7360
    CPad* GetInternalKeys();                    // 0xA73B0
    CPad* GetKeys();                            // 0xA73C0
    CPad* GetKeys(int player);                  // 0xA73D0
    void ResetKeys(int player);                 // 0xA73E0
    void ResetInternalKeys();                   // 0xA7400
}
