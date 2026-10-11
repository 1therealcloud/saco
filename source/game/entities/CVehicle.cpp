#include <stdafx.h>

#include "CVehicle.h"
#include "game/patch/patch.h"
#include "game/patch/hooks.h"

#include "game/ModelInfo.h"
#include "game/Scripting.h"
#include "game/render/CLicensePlate.h"
#include "main.h"

namespace game::patch::vehicle::detail
{
    UsesSirenFn originalUsesSiren = nullptr;
    VehicleAudioFn originalVehicleAudio = nullptr;
    bool nextVehicleHasSiren = false;
    bool detachingTrailer = false;
}

namespace
{
    using namespace game::patch::vehicle::detail;
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
    struct PlateTextures
    {
        RwTexture* original = nullptr;
        RwTexture* replacement = nullptr;
    };
    std::unordered_map<CVehicle*, PlateTextures> g_vehiclePlates;
    void* g_lastTrainVehicle = nullptr;

    CVehicle* VehicleWrapperFromPointer(void* vehicle);

    constexpr std::array<std::uintptr_t, 5> TRAILER_VTABLE_SLOTS = {
        0x871218, 0x871778, 0x8718D0, 0x871A40, 0x871BE0,
    };

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
        if (found == g_nativeVehicles.end())
            return nullptr;
        CVehicle* wrapper = found->second;
        return wrapper && wrapper->GetNativeVehicleHandle() == vehicle ? wrapper : nullptr;
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

bool CVehicle::HasNativeVehicle() const
{
    return m_pGameVehicle && VehicleFromHandle(m_handle) == m_pGameVehicle;
}

void* CVehicle::GetNativeVehicleHandle() const
{
    return HasNativeVehicle() ? m_pGameVehicle : nullptr;
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

        nextVehicleHasSiren = hasSiren != FALSE;
        scm::ProcessCommand(&CREATE_CAR, model, static_cast<double>(position.x), static_cast<double>(position.y), static_cast<double>(position.z), &m_handle);
        nextVehicleHasSiren = false;
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
    const void* original = m_pGameVehicle;
    m_pGameVehicle = VehicleFromHandle(m_handle) == original ? m_pGameVehicle : nullptr;
    m_pGameEntity = m_pGameVehicle;
    UnregisterVehicle(this);
    ShutdownLicensePlate();

    if (m_marker)
    {
        scm::ProcessCommand(&DISABLE_MARKER, m_marker);
        m_marker = 0;
    }

    if (!m_pGameVehicle)
    {
        if (g_lastTrainVehicle == original)
            g_lastTrainVehicle = nullptr;
        m_handle = 0;
        return;
    }

    RemovePassengers();

    if (m_pTrailer)
    {
        DetachTrailer();
        m_pTrailer = nullptr;
    }

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
    if (HasNativeVehicle())
        scm::ProcessCommand(&CHANGE_CAR_INTERIOR, m_handle, id);
}

void CVehicle::ResetPointers()
{
    void* native = VehicleFromHandle(m_handle);
    if (native != m_pGameVehicle)
        ShutdownLicensePlate();

    UnregisterVehicle(this);
    m_pGameVehicle = native;
    m_pGameEntity = native;
    RegisterVehicle(this);
}

BOOL CVehicle::HasDriver()
{
    if (!HasNativeVehicle())
        return FALSE;

    auto* driver = Read<void*>(m_pGameVehicle, 0x460);
    if (!driver)
        return FALSE;

    return (Read<std::uint32_t>(driver, 0x46C) & 0x100) && !Read<void*>(driver, 0x598);
}

bool CVehicle::HasPlayerDriver() const
{
    if (!m_pGameVehicle || VehicleFromHandle(m_handle) != m_pGameVehicle)
        return false;
    auto* driver = Read<void*>(m_pGameVehicle, 0x460);
    return driver && (Read<std::uint32_t>(driver, 0x46C) & 0x100u) != 0;
}

BOOL CVehicle::IsOccupied()
{
    if (!HasNativeVehicle())
        return FALSE;
    for (std::size_t offset = 0x460; offset < 0x480; offset += 4)
        if (Read<void*>(m_pGameVehicle, offset))
            return TRUE;
    return FALSE;
}

void CVehicle::SetInvulnerable(BOOL invulnerable)
{
    if (!HasNativeVehicle() || Read<std::uintptr_t>(m_pGameVehicle, 0) == 0x863C40)
        return;

    const int value = invulnerable ? 1 : 0;
    scm::ProcessCommand(&SET_CAR_PROOFS, m_handle, value, value, value, value, value);
    scm::ProcessCommand(&SET_CAR_DAMAGEABLE, m_handle, invulnerable ? 0 : 1);
    m_bIsInvulnerable = invulnerable ? TRUE : FALSE;
}

void CVehicle::SetLocked(BOOL locked)
{
    if (HasNativeVehicle())
        scm::ProcessCommand(&SET_CAR_LOCKED, m_handle, locked ? 1 : 0);
}

float CVehicle::GetHealth()
{
    return HasNativeVehicle() ? Read<float>(m_pGameVehicle, 0x4C0) : 0.0f;
}

void CVehicle::SetHealth(float value)
{
    if (HasNativeVehicle())
        Write<float>(m_pGameVehicle, 0x4C0, value);
}

void CVehicle::SetColor(unsigned char primary, unsigned char secondary)
{
    if (HasNativeVehicle())
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
    if (!HasNativeVehicle() || !m_bNeedsToUpdateColor || pad_71)
        return;
    if (Read<unsigned char>(m_pGameVehicle, 0x434) == m_nPrimaryColor && Read<unsigned char>(m_pGameVehicle, 0x435) == m_nSecondaryColor)
        return;
    Write<unsigned char>(m_pGameVehicle, 0x434, m_nPrimaryColor);
    Write<unsigned char>(m_pGameVehicle, 0x435, m_nSecondaryColor);
}

int CVehicle::GetSubtype()
{
    return HasNativeVehicle() ? VehicleSubtype(m_pGameVehicle) : 0;
}

BOOL CVehicle::IsSunk()
{
    return HasNativeVehicle() ? scm::ProcessCommand(&IS_CAR_SUNK, m_handle) : FALSE;
}

BOOL CVehicle::IsWrecked()
{
    return HasNativeVehicle() ? scm::ProcessCommand(&IS_CAR_WRECKED, m_handle) : FALSE;
}

BOOL CVehicle::DriverIsPlayerPed()
{
    if (!HasNativeVehicle())
        return FALSE;
    auto* player = *reinterpret_cast<void**>(0xB7CD98);
    return player && Read<void*>(m_pGameVehicle, 0x460) == player;
}

BOOL CVehicle::HasPlayerPed()
{
    if (!HasNativeVehicle())
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
    if (!HasNativeVehicle())
        return;
    auto flags = Read<unsigned char>(m_pGameVehicle, 0x42D);
    Write<unsigned char>(m_pGameVehicle, 0x42D, enable ? static_cast<unsigned char>(flags | 0x80) : static_cast<unsigned char>(flags & ~0x80));
}

void CVehicle::SetAlarmTime(std::uint16_t duration)
{
    if (DoesExist())
        Write<std::uint16_t>(m_pGameVehicle, 0x45C, duration);
}

void CVehicle::SetAlarmState(bool enable)
{
    if (!DoesExist())
        return;
    EnableSiren(enable);
    SetAlarmTime(enable ? 20000 : 0);
}

void CVehicle::SetBonnetState(bool open)
{
    OpenDoor(16, 0, open ? 1.0f : 0.0f);
}

void CVehicle::SetBootState(bool open)
{
    OpenDoor(17, 1, open ? 1.0f : 0.0f);
}

void CVehicle::SetObjective(bool enable)
{
    m_bIsObjective = enable;
    ProcessMarkers();
}

BOOL CVehicle::SirenEnabled()
{
    return HasNativeVehicle() && (Read<unsigned char>(m_pGameVehicle, 0x42D) & 0x80) != 0;
}

void CVehicle::SetLandingGearState(BOOL hide)
{
    if (!HasNativeVehicle() || GetSubtype() != 5)
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
    return HasNativeVehicle() && GetSubtype() == 5 && Read<float>(m_pGameVehicle, 0x9CC) != 0.0f;
}

void CVehicle::SetHydraThrusters(int direction)
{
    if (HasNativeVehicle())
        Write<int>(m_pGameVehicle, 0x86C, direction);
}

int CVehicle::GetHydraThrusters()
{
    return HasNativeVehicle() ? Read<int>(m_pGameVehicle, 0x86C) : 0;
}

void CVehicle::ProcessMarkers()
{
    if (!HasNativeVehicle())
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
        if (m_marker)
        {
            scm::ProcessCommand(&SET_MARKER_COLOR, m_marker, 1006);
            scm::ProcessCommand(&SET_MARKER_SCALE, m_marker, 3);
            m_bObjectiveBlipCreated = TRUE;
        }
        return;
    }

    if (m_bObjectiveBlipCreated)
    {
        if (m_marker)
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
            scm::ProcessCommand(&SET_MARKER_COLOR, m_marker, 1004);
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
    if (!HasNativeVehicle())
        return;
    Write<std::uint32_t>(m_pGameVehicle, 0x4F8, lock ? 2u : 0u);
    m_bIsLocked = lock ? TRUE : FALSE;
}

BOOL CVehicle::UpdateLastDrivenTime()
{
    if (!HasNativeVehicle() || !Read<void*>(m_pGameVehicle, 0x460))
        return FALSE;
    m_bHasBeenDriven = TRUE;
    m_timeSinceLastDriven = ::GetTickCount();
    return TRUE;
}

float CVehicle::GetTrainSpeed()
{
    return HasNativeVehicle() ? Read<float>(m_pGameVehicle, 0x5A4) : 0.0f;
}

void CVehicle::SetTurretRotation(float horizontal, float vertical)
{
    if (!DoesExist())
        return;
    Write<float>(m_pGameVehicle, 0x94C, horizontal);
    Write<float>(m_pGameVehicle, 0x950, vertical);
}

float CVehicle::GetTurretHorizontalRotation() const
{
    return m_pGameVehicle && VehicleFromHandle(m_handle) == m_pGameVehicle ? Read<float>(m_pGameVehicle, 0x94C) : 0.0f;
}

float CVehicle::GetTurretVerticalRotation() const
{
    return m_pGameVehicle && VehicleFromHandle(m_handle) == m_pGameVehicle ? Read<float>(m_pGameVehicle, 0x950) : 0.0f;
}

void CVehicle::SetTrainSpeed(float value)
{
    if (HasNativeVehicle() && value >= -100.0f && value <= 100.0f)
        Write<float>(m_pGameVehicle, 0x5A4, value);
}

void CVehicle::SetTires(char state)
{
    if (!HasNativeVehicle())
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
    if (!HasNativeVehicle())
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
    if (!HasNativeVehicle())
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
    return HasNativeVehicle() && GetSubtype() == 1 ? Read<int>(m_pGameVehicle, 0x5B4) : 0;
}

int CVehicle::GetDoorsDamage()
{
    return HasNativeVehicle() && GetSubtype() == 1 ? Read<int>(m_pGameVehicle, 0x5A9) : 0;
}

char CVehicle::GetLightsDamage()
{
    return HasNativeVehicle() && GetSubtype() == 1 ? static_cast<char>(Read<unsigned char>(m_pGameVehicle, 0x5B0)) : 0;
}

void CVehicle::AttachTrailer()
{
    if (!HasNativeVehicle() || !m_pTrailer || m_pTrailer == this || !m_pTrailer->HasNativeVehicle())
        return;

    if (GetTrailer() == m_pTrailer)
        return;

    if (CVehicle* previous = m_pTrailer->GetTractor(); previous && previous != this)
    {
        previous->SetTrailer(m_pTrailer);
        previous->DetachTrailer();
        previous->SetTrailer(nullptr);
    }

    scm::ProcessCommand(&ATTACH_TRAILER_TO_CAB, m_pTrailer->m_handle, m_handle);
}

void CVehicle::DetachTrailer()
{
    if (!HasNativeVehicle() || !m_pTrailer || !m_pTrailer->HasNativeVehicle())
        return;

    const bool wasDetaching = detachingTrailer;
    detachingTrailer = true;
    scm::ProcessCommand(&DETACH_TRAILER_FROM_CAB, m_pTrailer->m_handle, m_handle);
    detachingTrailer = wasDetaching;
}

void CVehicle::SetTrailer(CVehicle* vehicle)
{
    m_pTrailer = vehicle == this ? nullptr : vehicle;
}

CVehicle* CVehicle::GetTrailer()
{
    return HasNativeVehicle() ? VehicleWrapperFromPointer(Read<void*>(m_pGameVehicle, 0x4C8)) : nullptr;
}

CVehicle* CVehicle::GetTractor()
{
    if (!HasNativeVehicle())
        return nullptr;
    const int subtype = GetSubtype();
    if (subtype == 2 || subtype == 4 || subtype == 6 || subtype == 7)
        return nullptr;
    return VehicleWrapperFromPointer(Read<void*>(m_pGameVehicle, 0x4C4));
}

BOOL CVehicle::IsTrailer()
{
    if (!HasNativeVehicle())
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
    return HasNativeVehicle() && Read<std::uint16_t>(m_pGameVehicle, 0x22) == 525;
}

BOOL CVehicle::IsRC()
{
    if (!HasNativeVehicle())
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
    if (!HasNativeVehicle())
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
    if (!HasNativeVehicle())
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
    if (!HasNativeVehicle() || GetSubtype() != 1)
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
    if (!HasNativeVehicle())
        return FALSE;
    scm::ProcessCommand(&REMOVE_CAR_COMPONENT, m_handle, static_cast<int>(id));
    return TRUE;
}

void CVehicle::SetPaintjob(unsigned char id)
{
    if (HasNativeVehicle() && GetSubtype() == 1 && id <= 3)
        scm::ProcessCommand(&SET_CAR_PAINTJOB, m_handle, static_cast<int>(id));
}

BOOL CVehicle::DoesExist()
{
    return m_pGameVehicle && VehicleFromHandle(m_handle) == m_pGameVehicle;
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
    if (HasNativeVehicle())
        scm::ProcessCommand(&SET_CAR_Z_ANGLE, m_handle, static_cast<double>(value));
}

void CVehicle::ConstructLicensePlate()
{
    if (!HasNativeVehicle() || GetSubtype() != 1)
        return;

    if (!m_pLicensePlate && m_szLicensePlateText[0] && g_licensePlate)
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
        // gta owns one texture reference; saco keeps another until shutdown
        RwTextureAddRef(texture);
        g_vehiclePlates[this] = {Read<RwTexture*>(m_pGameVehicle, 0x588), texture};
        Write<RwTexture*>(m_pGameVehicle, 0x588, texture);
    }
}

void CVehicle::ShutdownLicensePlate()
{
    const auto it = g_vehiclePlates.find(this);
    if (it != g_vehiclePlates.end())
    {
        const auto state = it->second;
        const bool nativeAlive = m_pGameVehicle && VehicleFromHandle(m_handle) == m_pGameVehicle;
        const bool stillInstalled = nativeAlive && Read<RwTexture*>(m_pGameVehicle, 0x588) == state.replacement;
        if (stillInstalled)
        {
            Write<RwTexture*>(m_pGameVehicle, 0x588, state.original);
            if (state.replacement)
                RwTextureDestroy(state.replacement); // release gta's reference
        }
        else if (state.original)
        {
            // gta destroyed its installed plate, or another mod replaced it
            RwTextureDestroy(state.original);
        }
        if (state.replacement)
            RwTextureDestroy(state.replacement); // release saco's retained reference
        g_vehiclePlates.erase(it);
    }
    if (m_pLicensePlate)
    {
        m_pLicensePlate->Release();
        m_pLicensePlate = nullptr;
    }
}

void CVehicle::SetEngineOn(BOOL state)
{
    m_bEngineOn = state;
}

BOOL CVehicle::GetEngineOn() const
{
    return m_bEngineOn;
}

void CVehicle::SetLightsOn(BOOL state)
{
    m_bIsLightsOn = state;
}

BOOL CVehicle::GetLightsOn() const
{
    return m_bIsLightsOn;
}

void CVehicle::OpenDoor(int component, int door, float ratio)
{
    if (!DoesExist() || GetSubtype() != 1)
        return;

    reinterpret_cast<void(__thiscall*)(void*, void*, int, int, float, bool)>(0x6A6AE0)(m_pGameVehicle, nullptr, component, door, ratio, true);
}

BOOL CVehicle::HasSiren()
{
    return m_bHasSiren != FALSE;
}

char CVehicle::GetMaxPassengers()
{
    return HasNativeVehicle() ? static_cast<char>(Read<unsigned char>(m_pGameVehicle, 0x488)) : 0;
}

void CVehicle::SetDoorOpenState(unsigned char doorId, bool open)
{
    if (doorId >= 4)
        return;

    constexpr int components[]{10, 8, 11, 9};
    OpenDoor(components[doorId], 2 + doorId, open ? 1.0f : 0.0f);
}

void CVehicle::SetWindowOpenFlag(unsigned char component)
{
    if ((component == 10 || component == 8 || component == 11 || component == 9) && DoesExist() && GetSubtype() == 1)
        reinterpret_cast<void(__thiscall*)(void*, unsigned char)>(0x6D3080)(m_pGameVehicle, component);
}

void CVehicle::ClearWindowOpenFlag(unsigned char component)
{
    if ((component == 10 || component == 8 || component == 11 || component == 9) && DoesExist() && GetSubtype() == 1)
        reinterpret_cast<void(__thiscall*)(void*, unsigned char)>(0x6D30B0)(m_pGameVehicle, component);
}

void CVehicle::EnableEngine(BOOL enable)
{
    if (!HasNativeVehicle())
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

namespace game::patch::vehicle
{
    bool install()
    {
        static bool installed = false;
        if (installed)
            return true;

        if (!hook::install(0x6D8470, reinterpret_cast<detail::UsesSirenFn>(&UsesSirenHook), &detail::originalUsesSiren))
            return false;
        if (!hook::install(0x502280, reinterpret_cast<detail::VehicleAudioFn>(&VehicleAudioHook), &detail::originalVehicleAudio))
            return false;
        const auto replacement = reinterpret_cast<std::uintptr_t>(&TrailerProcessHook);
        for (const auto slot : TRAILER_VTABLE_SLOTS)
            if (!::patch::write(slot, replacement))
                return false;

        installed = true;
        return true;
    }
}

std::uint32_t CVehicle::GetNativeRef(void* vehicle)
{
    auto* pool = *reinterpret_cast<void**>(0xB74494);
    return pool && vehicle ? VehicleHandleFromPointer(vehicle) : 0xFFFFFFFFu;
}

int CVehicle::GetUnoccupiedSyncSeat(const void* localPed) const
{
    if (!HasNativeVehicle())
        return -1;
    for (int seat = 1; seat <= 7; ++seat)
    {
        void* occupant = Read<void*>(m_pGameVehicle, 0x460 + seat * 4);
        if (!occupant || Read<void*>(occupant, 0x598))
            continue;
        return occupant == localPed ? seat : -1;
    }
    return 0;
}

CVehicle* CVehicle::FindNative(void* vehicle)
{
    return VehicleWrapperFromPointer(vehicle);
}

void* CVehicle::GetDriver() const
{
    return HasNativeVehicle() ? Read<void*>(m_pGameVehicle, 0x460) : nullptr;
}
