#pragma once

#include "game/entities/CActor.h"
#include <cstddef>
#include <cstdint>

struct ActorInfo
{
    std::uint16_t m_nId = 0;
    int m_nModel = 0;
    CVector m_position{};
    float m_fRotation = 0;
    float m_fHealth = 100.0f;
    bool m_bInvulnerable = false;
};

class CActorPool
{
public:
    using ID = std::uint16_t;
    static constexpr int MAX_ACTORS = 1000;
    static constexpr ID INVALID_ID = 0xFFFF;

    int m_nLargestId = 0;
    CActor* m_pObject[MAX_ACTORS]{};
    BOOL m_bNotEmpty[MAX_ACTORS]{};
    void* m_pGameObject[MAX_ACTORS]{};
    int pad_2ee4[MAX_ACTORS]{};
    int pad_3e84[MAX_ACTORS]{};

    CActorPool();  // 0x16C0
    ~CActorPool(); // 0x18E0
    CActorPool(const CActorPool&) = delete;
    CActorPool& operator=(const CActorPool&) = delete;

    CActor* Get(ID id) const;           // 0x1610
    BOOL DoesExist(ID id) const;        // 0x1640
    void UpdateLargestId();             // 0x1660
    BOOL Delete(ID id);                 // 0x16F0
    ID Find(const void* gamePed) const; // 0x18B0
    BOOL Create(const ActorInfo* info); // 0x1900
};
