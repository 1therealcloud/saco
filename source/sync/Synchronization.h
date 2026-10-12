#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "utils/math/CVector.h"

#include "sync/Animation.h"
#include "ControllerState.h"

#pragma pack(push, 1)
namespace Synchronization
{
    using ID = std::uint16_t;

    constexpr std::uint8_t PackWeaponAndKeys(std::uint8_t weaponId, std::uint8_t extraKeys)
    {
        return static_cast<std::uint8_t>((weaponId & 0x3Fu) | ((extraKeys & 0x03u) << 6));
    }

    struct OnfootData
    {
        ControllerState m_controllerState;
        CVector m_position;
        float m_fQuaternion[4];
        std::uint8_t m_nHealth;
        std::uint8_t m_nArmor;
        std::uint8_t m_nCurrentWeapon;
        std::uint8_t m_nSpecialAction;
        CVector m_speed;
        CVector m_surfingOffset;
        ID m_nSurfingVehicleId;
        Animation m_animation;
    };

    struct IncarData
    {
        ID m_nVehicle;
        ControllerState m_controllerState;
        float m_fQuaternion[4];
        CVector m_position;
        CVector m_speed;
        float m_fHealth;
        std::uint8_t m_nDriverHealth;
        std::uint8_t m_nDriverArmor;
        std::uint8_t m_nCurrentWeapon;
        bool m_bSirenEnabled;
        bool m_bLandingGear;
        ID m_nTrailerId;
        union
        {
            std::uint16_t m_aHydraThrustAngle[2];
            float m_fTrainSpeed;
        };
    };

    struct AimData
    {
        enum WeaponState
        {
            WS_NO_BULLETS = 0,
            WS_LAST_BULLET = 1,
            WS_MORE_BULLETS = 2,
            WS_RELOADING = 3
        };

        std::uint8_t m_nCameraMode;
        CVector m_aimf1;
        CVector m_aimPos;
        float m_fAimZ;
        std::uint8_t m_nCameraExtZoom : 6;
        std::uint8_t m_nWeaponState : 2;
        char m_nAspectRatio;
    };

    struct TrailerData
    {
        ID m_nId;
        CVector m_position;
        float m_fQuaternion[4];
        CVector m_speed;
        CVector m_turnSpeed;
    };

    struct PassengerData
    {
        ID m_nVehicleId;
        std::uint8_t m_nSeatId;
        std::uint8_t m_nCurrentWeapon;
        std::uint8_t m_nHealth;
        std::uint8_t m_nArmor;
        ControllerState m_controllerState;
        CVector m_position;
    };

    struct UnoccupiedData
    {
        ID m_nVehicleId;
        std::uint8_t m_nSeatId;
        CVector m_roll;
        CVector m_direction;
        CVector m_position;
        CVector m_speed;
        CVector m_turnSpeed;
        float m_fHealth;
    };

    struct BulletData
    {
        std::uint8_t m_nTargetType;
        ID m_nTargetId;
        CVector m_origin;
        CVector m_target;
        CVector m_center;
        std::uint8_t m_nWeapon;
    };

    struct SpectatorData
    {
        ControllerState m_controllerState;
        CVector m_position;
    };

    struct StatsData
    {
        int m_nMoney;
        int m_nDrunkLevel;
    };

    inline char CompressAspectRatio(float value)
    {
        return static_cast<char>(value * 255.0f);
    }

    inline float DecompressAspectRatio(char value)
    {
        return static_cast<std::uint8_t>(value) / 255.0f;
    }

    inline std::uint8_t CompressCameraExtZoom(float value)
    {
        return static_cast<std::uint8_t>(value * 63.0f) & 63u;
    }

    inline float DecompressCameraExtZoom(std::uint8_t value)
    {
        return (value & 63u) / 63.0f;
    }
}
#pragma pack(pop)
static_assert(std::is_signed_v<char>);

static_assert(sizeof(Synchronization::OnfootData) == 0x44 && offsetof(Synchronization::OnfootData, m_position) == 0x06 &&
              offsetof(Synchronization::OnfootData, m_animation) == 0x40);

static_assert(sizeof(Synchronization::IncarData) == 0x3F && offsetof(Synchronization::IncarData, m_controllerState) == 0x02 &&
              offsetof(Synchronization::IncarData, m_fTrainSpeed) == 0x3B);

static_assert(sizeof(Synchronization::AimData) == 0x1F && offsetof(Synchronization::AimData, m_aimf1) == 0x01 && offsetof(Synchronization::AimData, m_nAspectRatio) == 0x1E);

static_assert(sizeof(Synchronization::TrailerData) == 0x36 && offsetof(Synchronization::TrailerData, m_position) == 0x02);

static_assert(sizeof(Synchronization::PassengerData) == 0x18 && offsetof(Synchronization::PassengerData, m_controllerState) == 0x06 &&
              offsetof(Synchronization::PassengerData, m_position) == 0x0C);

static_assert(sizeof(Synchronization::UnoccupiedData) == 0x43 && offsetof(Synchronization::UnoccupiedData, m_roll) == 0x03 &&
              offsetof(Synchronization::UnoccupiedData, m_fHealth) == 0x3F);

static_assert(sizeof(Synchronization::BulletData) == 0x28 && offsetof(Synchronization::BulletData, m_nTargetId) == 0x01 &&
              offsetof(Synchronization::BulletData, m_origin) == 0x03 && offsetof(Synchronization::BulletData, m_nWeapon) == 0x27);

static_assert(sizeof(Synchronization::SpectatorData) == 0x12 && offsetof(Synchronization::SpectatorData, m_position) == 0x06);

static_assert(sizeof(Synchronization::StatsData) == 0x08);
