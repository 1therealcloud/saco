#pragma once

#include <cstddef>
#include <cstdint>

#include "utils/math/CVector.h"

using GTAREF = std::uint32_t;

struct Pickup
{
    int m_nModel;
    int m_nType;
    CVector m_position;
};

struct WeaponPickup
{
    bool m_bExists;
    std::uint16_t m_nExOwner;
};

class CPickupPool
{
public:
    using ID = std::uint16_t;

    enum
    {
        MAX_PICKUPS = 4096
    };

    static constexpr int INVALID_INDEX = -1;

    int m_nCount;                        // 0x00000
    GTAREF m_handle[MAX_PICKUPS];        // 0x00004
    int m_nId[MAX_PICKUPS];              // 0x04004, GTA pickup index
    std::uint32_t m_nTimer[MAX_PICKUPS]; // 0x08004
    WeaponPickup m_weapon[MAX_PICKUPS];  // 0x0C004
    Pickup m_object[MAX_PICKUPS];        // 0x0F004

    CPickupPool();
    ~CPickupPool();

    CPickupPool(const CPickupPool&) = delete;
    CPickupPool& operator=(const CPickupPool&) = delete;

    void Create(Pickup* data, int id);

    // Creates a dropped weapon pickup owned by exOwner (the player it was dropped by).
    void CreateWeapon(int model, CVector position, int ammo, ID exOwner);

    void Delete(int id);

    // Deletes every dropped weapon pickup created for exOwner.
    void DeleteWeapon(ID exOwner);

    int GetIndex(int id) const;
    void SendNotification(int id);
    void Process();
};
