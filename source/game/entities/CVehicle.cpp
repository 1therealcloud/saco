#include <stdafx.h>

#include "CVehicle.h"

#include "game/ModelInfo.h"
#include "game/Scripting.h"
#include "game/render/CLicensePlate.h"
#include "main.h"

namespace
{
    constexpr scm::OpcodeInfo CREATE_CAR{0x00A5, "ifffv"};
    constexpr scm::OpcodeInfo DELETE_CAR{0x00A6, "i"};
    constexpr scm::OpcodeInfo SET_CAR_Z_ANGLE{0x0175, "if"};
    constexpr scm::OpcodeInfo CREATE_MARKER_ABOVE_CAR{0x0161, "iiiv"};
    constexpr scm::OpcodeInfo DISABLE_MARKER{0x0164, "i"};
    constexpr scm::OpcodeInfo SET_MARKER_COLOR{0x0165, "ii"};
    constexpr scm::OpcodeInfo SET_MARKER_SCALE{0x0168, "ii"};
    constexpr scm::OpcodeInfo IS_CAR_SUNK{0x02BF, "i"};
    constexpr scm::OpcodeInfo IS_CAR_WRECKED{0x0119, "i"};
    constexpr scm::OpcodeInfo SET_CAR_PROOFS{0x02AC, "iiiiii"};
    constexpr scm::OpcodeInfo WARP_CHAR_FROM_CAR_TO_COORD{0x0362, "ifff"};
    constexpr scm::OpcodeInfo CREATE_TRAIN{0x06D8, "ifffiv"};
    constexpr scm::OpcodeInfo DESTROY_TRAIN{0x07BD, "i"};
    constexpr scm::OpcodeInfo SET_CAR_LOCKED{0x0519, "ii"};
    constexpr scm::OpcodeInfo SET_CAR_DAMAGEABLE{0x053F, "ii"};
    constexpr scm::OpcodeInfo CHANGE_CAR_INTERIOR{0x0840, "ii"};
    constexpr scm::OpcodeInfo ATTACH_TRAILER_TO_CAB{0x0893, "ii"};
    constexpr scm::OpcodeInfo DETACH_TRAILER_FROM_CAB{0x07AC, "ii"};
    constexpr scm::OpcodeInfo ADD_CAR_COMPONENT{0x06E7, "iiv"};
    constexpr scm::OpcodeInfo IS_CAR_COMPONENT_AVAILABLE{0x06EA, "i"};
    constexpr scm::OpcodeInfo REQUEST_CAR_COMPONENT{0x06E9, "i"};
    constexpr scm::OpcodeInfo REMOVE_CAR_COMPONENT{0x06E8, "ii"};
    constexpr scm::OpcodeInfo SET_CAR_PAINTJOB{0x06ED, "ii"};
    constexpr scm::OpcodeInfo VEHICLE_CTOR_09C4{0x09C4, "ii"};
    constexpr scm::OpcodeInfo VEHICLE_CTOR_07FF{0x07FF, "ii"};

    constexpr int TRAIN_FREIGHT_LOCO = 538;
    constexpr int TRAIN_FREIGHT = 570;
    constexpr int TRAIN_STREAK_LOCO = 537;
    constexpr int TRAIN_STREAK = 569;
    constexpr int TRAIN_TRAM = 449;

    std::unordered_map<void*, CVehicle*> g_nativeVehicles;
    std::unordered_map<CVehicle*, RwTexture*> g_originalPlates;
    void* g_lastTrainVehicle = nullptr;

    template <class T>
    T Read(const void* data, std::size_t offset)
    {
        T value{};
        std::memcpy(&value, static_cast<const std::uint8_t*>(data) + offset, sizeof(value));
        return value;
    }

    template <class T>
    void Write(void* data, std::size_t offset, const T& value)
    {
        std::memcpy(static_cast<std::uint8_t*>(data) + offset, &value, sizeof(value));
    }

    void* VehicleFromHandle(std::uint32_t handle)
    {
        auto* pool = *reinterpret_cast<std::uint8_t**>(0xB74494);
        if (!pool || static_cast<std::int32_t>(handle) < 0)
            return nullptr;

        const auto index = handle >> 8;
        if (index >= *reinterpret_cast<const std::uint32_t*>(pool + 8))
            return nullptr;

        return reinterpret_cast<void*(__thiscall*)(void*, int)>(0x4048E0)(pool, static_cast<int>(handle));
    }

    std::uint32_t VehicleHandleFromPointer(void* vehicle)
    {
        auto* pool = *reinterpret_cast<void**>(0xB74494);
        return pool && vehicle ? reinterpret_cast<std::uint32_t(__thiscall*)(void*, void*)>(0x424160)(pool, vehicle) : 0;
    }

    std::uint32_t PedHandleFromPointer(void* ped)
    {
        auto* pool = *reinterpret_cast<void**>(0xB74490);
        return pool && ped ? reinterpret_cast<std::uint32_t(__thiscall*)(void*, void*)>(0x4442D0)(pool, ped) : 0;
    }

    CVehicle* VehicleWrapperFromPointer(void* vehicle)
    {
        if (!vehicle)
            return nullptr;
        const auto found = g_nativeVehicles.find(vehicle);
        return found != g_nativeVehicles.end() ? found->second : nullptr;
    }

    int VehicleSubtype(void* vehicle)
    {
        if (!vehicle)
            return 0;

        switch (Read<std::uintptr_t>(vehicle, 0))
        {
            case 0x871120:
                return 1;
            case 0x871360:
                return 2;
            case 0x871680:
                return 3;
            case 0x8721A0:
                return 4;
            case 0x871948:
                return 5;
            case 0x871528:
                return 6;
            case 0x872370:
                return 7;
            default:
                return 0;
        }
    }

    bool IsModelLoaded(int model)
    {
        auto* info = ModelInfo::GetRelocated(model);
        return info && Read<void*>(info, 0x1C);
    }

    bool EnsureModelLoaded(int model)
    {
        if (!ModelInfo::GetRelocated(model))
            return false;
        if (IsModelLoaded(model))
            return true;

        reinterpret_cast<void(__cdecl*)(int, int)>(0x4087E0)(model, 2);
        reinterpret_cast<void(__cdecl*)(bool)>(0x40EA10)(false);
        return IsModelLoaded(model);
    }

    int LoadedVehicleModelCount()
    {
        int count = 0;
        for (std::uintptr_t entry = 0x8E6C10; entry != 0x8E7C8C; entry += 0x14)
            if (*reinterpret_cast<void**>(entry))
                ++count;
        return count;
    }

    std::uint16_t ModelReferenceCount(int model)
    {
        auto* info = ModelInfo::GetRelocated(model);
        return info ? Read<std::uint16_t>(info, 8) : 0;
    }

    void RegisterVehicle(CVehicle* vehicle)
    {
        if (vehicle && vehicle->m_pGameVehicle)
            g_nativeVehicles[vehicle->m_pGameVehicle] = vehicle;
    }

    void UnregisterVehicle(CVehicle* vehicle)
    {
        if (!vehicle)
            return;
        for (auto it = g_nativeVehicles.begin(); it != g_nativeVehicles.end();)
        {
            if (it->second == vehicle)
                it = g_nativeVehicles.erase(it);
            else
                ++it;
        }
    }

    void RemoveTrainDriver(void* vehicle)
    {
        if (!vehicle)
            return;

        auto* driver = Read<void*>(vehicle, 0x460);
        if (!driver)
            return;

        const auto field598 = Read<std::uint32_t>(driver, 0x598);
        if (!field598 || field598 == 1)
            return;

        auto* vtable = Read<void**>(driver, 0);
        if (vtable && vtable[0])
            reinterpret_cast<void(__thiscall*)(void*, int)>(vtable[0])(driver, 1);
        Write<void*>(vehicle, 0x460, nullptr);
    }

    bool IsTrainModel(int model)
    {
        return model == TRAIN_FREIGHT_LOCO || model == TRAIN_FREIGHT || model == TRAIN_STREAK_LOCO || model == TRAIN_STREAK || model == TRAIN_TRAM;
    }
}

CVehicle::CVehicle(int model, CVector position, float rotation, BOOL keepModelLoaded, BOOL hasSiren)
{
    m_pTrailer = nullptr;
    m_pGameVehicle = nullptr;
    m_handle = 0;

    if (!IsTrainModel(model))
    {
        if (!EnsureModelLoaded(model))
            return;

        m_bHasSiren = hasSiren ? TRUE : FALSE;

        scm::ProcessCommand(&CREATE_CAR, model, static_cast<double>(position.x), static_cast<double>(position.y), static_cast<double>(position.z), &m_handle);
        scm::ProcessCommand(&SET_CAR_Z_ANGLE, m_handle, static_cast<double>(rotation));
        scm::ProcessCommand(&VEHICLE_CTOR_09C4, m_handle, 0);
        scm::ProcessCommand(&VEHICLE_CTOR_07FF, m_handle, 0);
        scm::ProcessCommand(&SET_CAR_DAMAGEABLE, m_handle, 0);

        m_pGameVehicle = VehicleFromHandle(m_handle);
        m_pGameEntity = m_pGameVehicle;
        if (!m_pGameVehicle)
            return;

        Write<std::uint32_t>(m_pGameVehicle, 0x4F8, 0);
        Write<std::uint32_t>(m_pGameVehicle, 0x4B0, 0);
        pad_5c = 0;

        CMatrix matrix = CMatrix::Unity();
        GetMatrix(&matrix);
        matrix.GetPosition() = position;
        if (GetSubtype() != 2 && GetSubtype() != 6)
            matrix.GetPosition().z += 0.25f;
        SetMatrix(matrix);

        m_bKeepModelLoaded = keepModelLoaded;
    }
    else if (model == TRAIN_FREIGHT || model == TRAIN_STREAK)
    {
        if (!g_lastTrainVehicle)
            return;

        m_pGameVehicle = Read<void*>(g_lastTrainVehicle, 0x5D4);
        if (!m_pGameVehicle)
        {
            g_lastTrainVehicle = nullptr;
            return;
        }

        m_handle = VehicleHandleFromPointer(m_pGameVehicle);
        m_pGameEntity = m_pGameVehicle;
        g_lastTrainVehicle = m_pGameVehicle;
    }
    else
    {
        const int trainType = model == TRAIN_FREIGHT_LOCO ? 5 : model == TRAIN_STREAK_LOCO ? 3 : 9;
        const BOOL direction = rotation > 180.0f ? TRUE : FALSE;
        constexpr int trainModels[] = {TRAIN_FREIGHT_LOCO, TRAIN_FREIGHT, TRAIN_STREAK_LOCO, TRAIN_STREAK, TRAIN_TRAM};

        for (int trainModel : trainModels)
            if (!ModelInfo::GetRelocated(trainModel))
                return;
        for (int trainModel : trainModels)
            if (!IsModelLoaded(trainModel))
                reinterpret_cast<void(__cdecl*)(int, int)>(0x4087E0)(trainModel, 2);
        reinterpret_cast<void(__cdecl*)(bool)>(0x40EA10)(false);
        for (int trainModel : trainModels)
            if (!IsModelLoaded(trainModel))
                return;

        scm::ProcessCommand(&CREATE_TRAIN, trainType, static_cast<double>(position.x), static_cast<double>(position.y), static_cast<double>(position.z), direction, &m_handle);
        m_pGameVehicle = VehicleFromHandle(m_handle);
        m_pGameEntity = m_pGameVehicle;
        g_lastTrainVehicle = m_pGameVehicle;
        RemoveTrainDriver(m_pGameVehicle);
    }

    m_bIsInvulnerable = FALSE;
    m_bIsObjective = false;
    m_bObjectiveBlipCreated = FALSE;
    m_marker = 0;
    m_bHasBeenDriven = FALSE;
    m_timeSinceLastDriven = ::GetTickCount();
    m_bEngineOn = -1;
    m_bIsLightsOn = -1;
    m_bIsLocked = FALSE;
    m_bNeedsToUpdateColor = FALSE;
    pad_71 = 0;
    m_bEngineState = FALSE;
    m_pLicensePlate = nullptr;
    std::memset(m_szLicensePlateText, 0, sizeof(m_szLicensePlateText));

    RegisterVehicle(this);
}

CVehicle::~CVehicle()
{
    m_pGameVehicle = VehicleFromHandle(m_handle);
    m_pGameEntity = m_pGameVehicle;
    UnregisterVehicle(this);
    ShutdownLicensePlate();

    if (!m_pGameVehicle)
        return;

    if (m_marker)
    {
        scm::ProcessCommand(&DISABLE_MARKER, m_marker);
        m_marker = 0;
    }

    RemovePassengers();

    if (m_pTrailer)
    {
        DetachTrailer();
        m_pTrailer = nullptr;
    }

    ShutdownLicensePlate();

    const int model = Read<std::uint16_t>(m_pGameVehicle, 0x22);
    if (model == TRAIN_FREIGHT_LOCO || model == TRAIN_STREAK_LOCO)
    {
        scm::ProcessCommand(&DESTROY_TRAIN, m_handle);
    }
    else
    {
        scm::ProcessCommand(&DELETE_CAR, m_handle);
        if (ModelReferenceCount(model) == 0 && !m_bKeepModelLoaded && LoadedVehicleModelCount() > 80 && IsModelLoaded(model))
            reinterpret_cast<void(__cdecl*)(int)>(0x4089A0)(model);
    }

    if (g_lastTrainVehicle == m_pGameVehicle)
        g_lastTrainVehicle = nullptr;

    m_pGameVehicle = nullptr;
    m_pGameEntity = nullptr;
    m_handle = 0;
}

void CVehicle::Add()
{
    if (!CEntity::DoesExist())
    {
        CEntity::Add();
        if (auto* trailer = GetTrailer())
            trailer->Add();
    }
}

void CVehicle::Remove()
{
    if (CEntity::DoesExist())
    {
        if (auto* trailer = GetTrailer())
            trailer->Remove();
        CEntity::Remove();
    }
}

void CVehicle::ChangeInterior(int id)
{
    if (VehicleFromHandle(m_handle))
        scm::ProcessCommand(&CHANGE_CAR_INTERIOR, m_handle, id);
}

void CVehicle::ResetPointers()
{
    if (!m_pGameVehicle)
        return;
    m_pGameVehicle = VehicleFromHandle(m_handle);
    m_pGameEntity = m_pGameVehicle;
}

BOOL CVehicle::HasDriver()
{
    if (!m_pGameVehicle || !VehicleFromHandle(m_handle))
        return FALSE;

    auto* driver = Read<void*>(m_pGameVehicle, 0x460);
    if (!driver)
        return FALSE;

    return (Read<std::uint32_t>(driver, 0x46C) & 0x100) && !Read<void*>(driver, 0x598);
}

BOOL CVehicle::IsOccupied()
{
    if (!m_pGameVehicle)
        return FALSE;
    for (std::size_t offset = 0x460; offset < 0x480; offset += 4)
        if (Read<void*>(m_pGameVehicle, offset))
            return TRUE;
    return FALSE;
}

void CVehicle::SetInvulnerable(BOOL invulnerable)
{
    if (!m_pGameVehicle || !VehicleFromHandle(m_handle) || Read<std::uintptr_t>(m_pGameVehicle, 0) == 0x863C40)
        return;

    const int value = invulnerable ? 1 : 0;
    scm::ProcessCommand(&SET_CAR_PROOFS, m_handle, value, value, value, value, value);
    scm::ProcessCommand(&SET_CAR_DAMAGEABLE, m_handle, invulnerable ? 0 : 1);
    m_bIsInvulnerable = invulnerable ? TRUE : FALSE;
}

void CVehicle::SetLocked(BOOL locked)
{
    if (m_pGameVehicle)
        scm::ProcessCommand(&SET_CAR_LOCKED, m_handle, locked ? 1 : 0);
}

float CVehicle::GetHealth()
{
    return m_pGameVehicle ? Read<float>(m_pGameVehicle, 0x4C0) : 0.0f;
}

void CVehicle::SetHealth(float value)
{
    if (m_pGameVehicle)
        Write<float>(m_pGameVehicle, 0x4C0, value);
}

void CVehicle::SetColor(unsigned char primary, unsigned char secondary)
{
    if (m_pGameVehicle && VehicleFromHandle(m_handle))
    {
        Write<unsigned char>(m_pGameVehicle, 0x434, primary);
        Write<unsigned char>(m_pGameVehicle, 0x435, secondary);
    }
    m_nPrimaryColor = primary;
    m_nSecondaryColor = secondary;
    m_bNeedsToUpdateColor = TRUE;
}

void CVehicle::UpdateColor()
{
    if (!m_pGameVehicle || !VehicleFromHandle(m_handle) || !m_bNeedsToUpdateColor || pad_71)
        return;
    if (Read<unsigned char>(m_pGameVehicle, 0x434) == m_nPrimaryColor && Read<unsigned char>(m_pGameVehicle, 0x435) == m_nSecondaryColor)
        return;
    Write<unsigned char>(m_pGameVehicle, 0x434, m_nPrimaryColor);
    Write<unsigned char>(m_pGameVehicle, 0x435, m_nSecondaryColor);
}

int CVehicle::GetSubtype()
{
    return VehicleSubtype(m_pGameVehicle);
}

BOOL CVehicle::IsSunk()
{
    return m_pGameVehicle ? scm::ProcessCommand(&IS_CAR_SUNK, m_handle) : FALSE;
}

BOOL CVehicle::IsWrecked()
{
    return m_pGameVehicle ? scm::ProcessCommand(&IS_CAR_WRECKED, m_handle) : FALSE;
}

BOOL CVehicle::DriverIsPlayerPed()
{
    if (!m_pGameVehicle)
        return FALSE;
    auto* player = *reinterpret_cast<void**>(0xB7CD98);
    return player && Read<void*>(m_pGameVehicle, 0x460) == player;
}

BOOL CVehicle::HasPlayerPed()
{
    if (!m_pGameVehicle)
        return FALSE;
    auto* player = *reinterpret_cast<void**>(0xB7CD98);
    return player && (Read<std::uint32_t>(player, 0x46C) & 0x100) && Read<void*>(player, 0x58C) == m_pGameVehicle;
}

BOOL CVehicle::IsTrainPart()
{
    const int model = GetModelIndex();
    return model == TRAIN_FREIGHT_LOCO || model == TRAIN_FREIGHT || model == TRAIN_STREAK_LOCO || model == TRAIN_STREAK || model == TRAIN_TRAM;
}

BOOL CVehicle::HasTurret()
{
    switch (GetModelIndex())
    {
        case 432:
        case 564:
        case 407:
        case 601:
            return TRUE;
        default:
            return FALSE;
    }
}

void CVehicle::EnableSiren(bool enable)
{
    auto flags = Read<unsigned char>(m_pGameVehicle, 0x42D);
    Write<unsigned char>(m_pGameVehicle, 0x42D, enable ? static_cast<unsigned char>(flags | 0x80) : static_cast<unsigned char>(flags & ~0x80));
}

void CVehicle::SetAlarmState(bool enable)
{
    if (!m_pGameVehicle)
        return;
    Write<std::uint16_t>(m_pGameVehicle, 0x45C, enable ? 0xFFFFu : 0u);
}

void CVehicle::SetBonnetState(bool open)
{
    if (!m_pGameVehicle || GetSubtype() != 1)
        return;
    auto* vtable = Read<void**>(m_pGameVehicle, 0);
    if (!vtable || !vtable[27])
        return;
    reinterpret_cast<void(__thiscall*)(void*, void*, int, int, float, bool)>(vtable[27])(m_pGameVehicle, nullptr, 16, 0, open ? 1.0f : 0.0f, false);
}

void CVehicle::SetBootState(bool open)
{
    if (!m_pGameVehicle || GetSubtype() != 1)
        return;
    auto* vtable = Read<void**>(m_pGameVehicle, 0);
    if (!vtable || !vtable[27])
        return;
    reinterpret_cast<void(__thiscall*)(void*, void*, int, int, float, bool)>(vtable[27])(m_pGameVehicle, nullptr, 17, 1, open ? 1.0f : 0.0f, false);
}

void CVehicle::SetObjective(bool enable)
{
    m_bIsObjective = enable;
    ProcessMarkers();
}

BOOL CVehicle::SirenEnabled()
{
    return (Read<unsigned char>(m_pGameVehicle, 0x42D) & 0x80) != 0;
}

void CVehicle::SetLandingGearState(BOOL hide)
{
    if (!m_pGameVehicle || GetSubtype() != 5)
        return;

    const float state = Read<float>(m_pGameVehicle, 0x9CC);
    if (hide)
    {
        if (state == 0.0f)
            reinterpret_cast<void(__thiscall*)(void*)>(0x6CAC20)(m_pGameVehicle);
    }
    else if (state == 1.0f)
    {
        reinterpret_cast<void(__thiscall*)(void*)>(0x6CAC70)(m_pGameVehicle);
    }
}

BOOL CVehicle::GetLandingGearState()
{
    return m_pGameVehicle && GetSubtype() == 5 && Read<float>(m_pGameVehicle, 0x9CC) != 0.0f;
}

void CVehicle::SetHydraThrusters(int direction)
{
    if (m_pGameVehicle)
        Write<int>(m_pGameVehicle, 0x86C, direction);
}

int CVehicle::GetHydraThrusters()
{
    return m_pGameVehicle ? Read<int>(m_pGameVehicle, 0x86C) : 0;
}

void CVehicle::ProcessMarkers()
{
    if (!m_pGameVehicle)
        return;

    if (m_bIsObjective)
    {
        if (m_bObjectiveBlipCreated)
            return;
        if (m_marker)
        {
            scm::ProcessCommand(&DISABLE_MARKER, m_marker);
            m_marker = 0;
        }
        scm::ProcessCommand(&CREATE_MARKER_ABOVE_CAR, m_handle, 1, 3, &m_marker);
        // r5's radar colour translator maps 1006 to this rgba value
        scm::ProcessCommand(&SET_MARKER_COLOR, m_marker, static_cast<int>(0xE2C063FFu));
        scm::ProcessCommand(&SET_MARKER_SCALE, m_marker, 3);
        m_bObjectiveBlipCreated = TRUE;
        return;
    }

    if (m_bObjectiveBlipCreated && m_marker)
    {
        scm::ProcessCommand(&DISABLE_MARKER, m_marker);
        m_bObjectiveBlipCreated = FALSE;
        m_marker = 0;
    }

    const float distance = GetDistanceToLocalPlayer();
    if (distance < 200.0f && !IsOccupied())
    {
        if (!m_marker)
        {
            scm::ProcessCommand(&CREATE_MARKER_ABOVE_CAR, m_handle, 1, 2, &m_marker);
            // r5's radar colour translator maps 1004 to this rgba value
            scm::ProcessCommand(&SET_MARKER_COLOR, m_marker, static_cast<int>(0x89A4C440u));
            scm::ProcessCommand(&SET_MARKER_SCALE, m_marker, 2);
        }
        return;
    }

    if ((IsOccupied() || GetDistanceToLocalPlayer() >= 200.0f) && m_marker)
    {
        scm::ProcessCommand(&DISABLE_MARKER, m_marker);
        m_marker = 0;
    }
}

void CVehicle::Lock(BOOL lock)
{
    Write<std::uint32_t>(m_pGameVehicle, 0x4F8, lock ? 2u : 0u);
    m_bIsLocked = lock ? TRUE : FALSE;
}

BOOL CVehicle::UpdateLastDrivenTime()
{
    if (!m_pGameVehicle || !Read<void*>(m_pGameVehicle, 0x460))
        return FALSE;
    m_bHasBeenDriven = TRUE;
    m_timeSinceLastDriven = ::GetTickCount();
    return TRUE;
}

float CVehicle::GetTrainSpeed()
{
    return m_pGameVehicle ? Read<float>(m_pGameVehicle, 0x5A4) : 0.0f;
}

void CVehicle::SetTrainSpeed(float value)
{
    if (m_pGameVehicle && value >= -100.0f && value <= 100.0f)
        Write<float>(m_pGameVehicle, 0x5A4, value);
}

void CVehicle::SetTires(char state)
{
    if (!m_pGameVehicle)
        return;

    const unsigned char value = static_cast<unsigned char>(state);
    if (GetSubtype() == 1)
    {
        Write<unsigned char>(m_pGameVehicle, 0x5A8, (value & 1) ? 1 : 0);
        Write<unsigned char>(m_pGameVehicle, 0x5A7, (value & 2) ? 1 : 0);
        Write<unsigned char>(m_pGameVehicle, 0x5A6, (value & 4) ? 1 : 0);
        Write<unsigned char>(m_pGameVehicle, 0x5A5, (value & 8) ? 1 : 0);
    }
    else if (GetSubtype() == 2)
    {
        Write<unsigned char>(m_pGameVehicle, 0x65D, (value & 1) ? 1 : 0);
        Write<unsigned char>(m_pGameVehicle, 0x65C, (value & 2) ? 1 : 0);
    }
}

char CVehicle::GetTires()
{
    if (!m_pGameVehicle)
        return 0;

    unsigned char state = 0;
    if (GetSubtype() == 1)
    {
        if (Read<unsigned char>(m_pGameVehicle, 0x5A5))
            state = 1;
        state <<= 1;
        if (Read<unsigned char>(m_pGameVehicle, 0x5A6))
            state |= 1;
        state <<= 1;
        if (Read<unsigned char>(m_pGameVehicle, 0x5A7))
            state |= 1;
        state <<= 1;
        if (Read<unsigned char>(m_pGameVehicle, 0x5A8))
            state |= 1;
    }
    else if (GetSubtype() == 2)
    {
        if (Read<unsigned char>(m_pGameVehicle, 0x65C))
            state = 1;
        state <<= 1;
        if (Read<unsigned char>(m_pGameVehicle, 0x65D))
            state |= 1;
    }
    return static_cast<char>(state);
}

void CVehicle::UpdateDamage(int panels, int doors, char lights)
{
    if (!m_pGameVehicle)
        return;

    const int subtype = GetSubtype();
    if (subtype != 1 && subtype != 5)
        return;

    if (!panels && !doors && !lights && (Read<int>(m_pGameVehicle, 0x5B4) || Read<int>(m_pGameVehicle, 0x5A9) || Read<int>(m_pGameVehicle, 0x5B0)))
    {
        reinterpret_cast<void(__thiscall*)(void*)>(0x6A3440)(m_pGameVehicle);
        return;
    }

    Write<int>(m_pGameVehicle, 0x5B4, panels);
    Write<int>(m_pGameVehicle, 0x5A9, doors);
    Write<std::uint32_t>(m_pGameVehicle, 0x5B0, static_cast<unsigned char>(lights));
    reinterpret_cast<void(__thiscall*)(void*)>(0x6B3E90)(m_pGameVehicle);
}

int CVehicle::GetPanelsDamage()
{
    return m_pGameVehicle && GetSubtype() == 1 ? Read<int>(m_pGameVehicle, 0x5B4) : 0;
}

int CVehicle::GetDoorsDamage()
{
    return m_pGameVehicle && GetSubtype() == 1 ? Read<int>(m_pGameVehicle, 0x5A9) : 0;
}

char CVehicle::GetLightsDamage()
{
    return m_pGameVehicle && GetSubtype() == 1 ? static_cast<char>(Read<unsigned char>(m_pGameVehicle, 0x5B0)) : 0;
}

void CVehicle::AttachTrailer()
{
    if (m_pTrailer)
        scm::ProcessCommand(&ATTACH_TRAILER_TO_CAB, m_pTrailer->m_handle, m_handle);
}

void CVehicle::DetachTrailer()
{
    if (m_pTrailer && m_pTrailer->m_handle && VehicleFromHandle(m_pTrailer->m_handle) && m_pTrailer->m_pGameVehicle)
        scm::ProcessCommand(&DETACH_TRAILER_FROM_CAB, m_pTrailer->m_handle, m_handle);
}

void CVehicle::SetTrailer(CVehicle* vehicle)
{
    m_pTrailer = vehicle;
}

CVehicle* CVehicle::GetTrailer()
{
    return m_pGameVehicle ? VehicleWrapperFromPointer(Read<void*>(m_pGameVehicle, 0x4C8)) : nullptr;
}

CVehicle* CVehicle::GetTractor()
{
    if (!m_pGameVehicle)
        return nullptr;
    const int subtype = GetSubtype();
    if (subtype == 2 || subtype == 4 || subtype == 6 || subtype == 7)
        return nullptr;
    return VehicleWrapperFromPointer(Read<void*>(m_pGameVehicle, 0x4C4));
}

BOOL CVehicle::IsTrailer()
{
    if (!m_pGameVehicle || !VehicleFromHandle(m_handle))
        return FALSE;
    switch (Read<std::uint16_t>(m_pGameVehicle, 0x22))
    {
        case 435:
        case 450:
        case 584:
        case 591:
        case 606:
        case 607:
        case 608:
        case 610:
        case 611:
            return TRUE;
        default:
            return FALSE;
    }
}

BOOL CVehicle::IsTowtruck()
{
    return m_pGameVehicle && VehicleFromHandle(m_handle) && Read<std::uint16_t>(m_pGameVehicle, 0x22) == 525;
}

BOOL CVehicle::IsRC()
{
    if (!m_pGameVehicle || !VehicleFromHandle(m_handle))
        return FALSE;
    switch (Read<std::uint16_t>(m_pGameVehicle, 0x22))
    {
        case 441:
        case 464:
        case 465:
        case 594:
        case 501:
        case 564:
            return TRUE;
        default:
            return FALSE;
    }
}

void CVehicle::EnableLights(bool enable)
{
    if (!m_pGameVehicle || !VehicleFromHandle(m_handle))
        return;

    auto flags4A8 = Read<unsigned char>(m_pGameVehicle, 0x4A8);
    auto flags428 = Read<unsigned char>(m_pGameVehicle, 0x428);
    if (enable)
    {
        flags4A8 &= static_cast<unsigned char>(~8u);
        flags428 |= 0x40;
    }
    else
    {
        flags4A8 |= 8;
        flags428 &= static_cast<unsigned char>(~0x40u);
    }
    Write<unsigned char>(m_pGameVehicle, 0x4A8, flags4A8);
    Write<unsigned char>(m_pGameVehicle, 0x428, flags428);
}

void CVehicle::RemovePassengers()
{
    if (!m_pGameVehicle || !VehicleFromHandle(m_handle))
        return;

    auto* matrix = Read<void*>(m_pGameVehicle, 0x14);
    if (!matrix)
        return;

    const float x = Read<float>(matrix, 0x30);
    const float y = Read<float>(matrix, 0x34);
    const float z = Read<float>(matrix, 0x38) + 2.0f;

    auto removePed = [&](void* ped) {
        if (!ped)
            return;
        const auto handle = PedHandleFromPointer(ped);
        scm::ProcessCommand(&WARP_CHAR_FROM_CAR_TO_COORD, handle, static_cast<double>(x), static_cast<double>(y), static_cast<double>(z));
    };

    removePed(Read<void*>(m_pGameVehicle, 0x460));
    for (std::size_t offset = 0x464; offset < 0x480; offset += 4)
        removePed(Read<void*>(m_pGameVehicle, offset));
}

BOOL CVehicle::AddComponent(unsigned short id)
{
    if (!m_pGameVehicle || !VehicleFromHandle(m_handle) || GetSubtype() != 1)
        return FALSE;

    if (!scm::ProcessCommand(&IS_CAR_COMPONENT_AVAILABLE, static_cast<int>(id)))
    {
        reinterpret_cast<void(__cdecl*)(int, int)>(0x4087E0)(static_cast<int>(id), 2);
        reinterpret_cast<void(__cdecl*)(bool)>(0x40EA10)(false);
        scm::ProcessCommand(&REQUEST_CAR_COMPONENT, static_cast<int>(id));
        for (int i = 0; i < 20 && !scm::ProcessCommand(&IS_CAR_COMPONENT_AVAILABLE, static_cast<int>(id)); ++i)
            ::Sleep(1);
    }

    if (!scm::ProcessCommand(&IS_CAR_COMPONENT_AVAILABLE, static_cast<int>(id)))
        return FALSE;

    int component = 0;
    scm::ProcessCommand(&ADD_CAR_COMPONENT, m_handle, static_cast<int>(id), &component);
    return TRUE;
}

BOOL CVehicle::RemoveComponent(unsigned short id)
{
    if (!m_pGameVehicle || !VehicleFromHandle(m_handle))
        return FALSE;
    scm::ProcessCommand(&REMOVE_CAR_COMPONENT, m_handle, static_cast<int>(id));
    return TRUE;
}

void CVehicle::SetPaintjob(unsigned char id)
{
    if (m_pGameVehicle && VehicleFromHandle(m_handle) && GetSubtype() == 1 && id <= 3)
        scm::ProcessCommand(&SET_CAR_PAINTJOB, m_handle, static_cast<int>(id));
}

BOOL CVehicle::DoesExist()
{
    return VehicleFromHandle(m_handle) != nullptr;
}

void CVehicle::SetLicensePlateText(const char* text)
{
    if (!text)
        return;
    ShutdownLicensePlate();
    std::strncpy(m_szLicensePlateText, text, MAX_LICENSE_PLATE_TEXT);
    m_szLicensePlateText[MAX_LICENSE_PLATE_TEXT] = '\0';
}

void CVehicle::SetRotation(float value)
{
    if (VehicleFromHandle(m_handle))
        scm::ProcessCommand(&SET_CAR_Z_ANGLE, m_handle, static_cast<double>(value));
}

void CVehicle::ConstructLicensePlate()
{
    if (!m_pLicensePlate && m_szLicensePlateText[0] && g_licensePlate && VehicleFromHandle(m_handle) == m_pGameVehicle)
    {
        m_pLicensePlate = g_licensePlate->Create(m_szLicensePlateText);
        if (!m_pLicensePlate)
            return;
        auto* texture = g_licensePlate->CreateRwTexture();
        if (!texture)
        {
            m_pLicensePlate->Release();
            m_pLicensePlate = nullptr;
            return;
        }
        // CAutomobile::CustomCarPlate_BeforeRenderingStart reads CVehicle + 0x588
        g_originalPlates.emplace(this, Read<RwTexture*>(m_pGameVehicle, 0x588));
        Write<RwTexture*>(m_pGameVehicle, 0x588, texture);
    }
}

void CVehicle::ShutdownLicensePlate()
{
    const auto original = g_originalPlates.find(this);
    if (original != g_originalPlates.end())
    {
        if (m_pGameVehicle && VehicleFromHandle(m_handle) == m_pGameVehicle)
        {
            if (auto* texture = Read<RwTexture*>(m_pGameVehicle, 0x588))
                RwTextureDestroy(texture);
            Write<RwTexture*>(m_pGameVehicle, 0x588, original->second);
        }
        else if (original->second)
            RwTextureDestroy(original->second);
        g_originalPlates.erase(original);
    }
    if (m_pLicensePlate)
    {
        m_pLicensePlate->Release();
        m_pLicensePlate = nullptr;
    }
}

BOOL CVehicle::HasSiren()
{
    return m_bHasSiren != FALSE;
}

char CVehicle::GetMaxPassengers()
{
    return m_pGameVehicle && VehicleFromHandle(m_handle) ? static_cast<char>(Read<unsigned char>(m_pGameVehicle, 0x488)) : 0;
}

void CVehicle::SetWindowOpenFlag(unsigned char doorId)
{
    if (m_pGameVehicle)
        reinterpret_cast<void(__thiscall*)(void*, unsigned char)>(0x6D3080)(m_pGameVehicle, doorId);
}

void CVehicle::ClearWindowOpenFlag(unsigned char doorId)
{
    if (m_pGameVehicle)
        reinterpret_cast<void(__thiscall*)(void*, unsigned char)>(0x6D30B0)(m_pGameVehicle, doorId);
}

void CVehicle::EnableEngine(BOOL enable)
{
    if (!m_pGameVehicle)
        return;

    auto flags = Read<unsigned char>(m_pGameVehicle, 0x428);
    if (!enable)
    {
        Write<unsigned char>(m_pGameVehicle, 0x428, static_cast<unsigned char>(flags & ~0x10u));
        if (GetSubtype() == 3 && m_bEngineState)
        {
            if (GetHealth() > 0.0f)
            {
                const auto saved = *reinterpret_cast<std::uint32_t*>(0xB6B990);
                reinterpret_cast<void(__thiscall*)(void*)>(0x4FCF40)(static_cast<std::uint8_t*>(m_pGameVehicle) + 0x138);
                *reinterpret_cast<std::uint32_t*>(0xB6B990) = saved;
            }
            m_bEngineState = FALSE;
        }
        return;
    }

    Write<unsigned char>(m_pGameVehicle, 0x428, static_cast<unsigned char>(flags | 0x10));
    if (GetSubtype() == 3 && HasDriver() && !DriverIsPlayerPed())
        m_bEngineState = TRUE;
}
