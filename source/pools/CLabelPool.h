#pragma once

#include <Windows.h>
#include <d3d9.h>

#include <cstddef>
#include <cstdint>

#include "utils/math/CVector.h"

class CLabelPool
{
public:
    using ID = std::uint16_t;

    enum
    {
        MAX_LABELS = 2048
    };

    static constexpr ID INVALID_ID = 0xFFFF;

    struct TextLabel
    {
        char* m_pText;           // 0x00
        D3DCOLOR m_color;        // 0x04, stored as AARRGGBB
        CVector m_position;      // 0x08
        float m_fDrawDistance;   // 0x14
        bool m_bBehindWalls;     // 0x18, R5 passes this as the LOS-test flag
        ID m_nAttachedToPlayer;  // 0x19
        ID m_nAttachedToVehicle; // 0x1B
    };

public:
    TextLabel m_object[MAX_LABELS]; // 0x0000
    BOOL m_bNotEmpty[MAX_LABELS];   // 0xE800

public:
    CLabelPool();
    ~CLabelPool();

    CLabelPool(const CLabelPool&) = delete;
    CLabelPool& operator=(const CLabelPool&) = delete;

    void Create(ID id, const char* text, D3DCOLOR color, CVector position, float drawDistance, bool behindWalls, ID attachedToPlayer, ID attachedToVehicle);
    BOOL Delete(ID id);
    void Draw();
};
