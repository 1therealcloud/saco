#include <stdafx.h>

#include "CPed.h"
#include "game/CGame.h"
#include "main.h"
#include "game/PlayerSlots.h"
#include "sync/ControllerState.h"
#include "sync/KeyStuff.h"
#include "game/CPad.h"

#include "CObject.h"
#include "game/ModelInfo.h"
#include "game/Scripting.h"
#include "sync/AimStuff.h"

#include <new>
#include <cmath>

namespace
{
    constexpr float RADIANS = 0.017453292f;
    constexpr float DEGREES = 57.295776f;

    constexpr scm::OpcodeInfo CREATE_PLAYER{0x0053, "vfffv"};
    constexpr scm::OpcodeInfo CREATE_ACTOR_FROM_PLAYER{0x01F5, "vv"};
    constexpr scm::OpcodeInfo SET_ACTOR_Z_ANGLE{0x0173, "if"};
    constexpr scm::OpcodeInfo DISABLE_MARKER{0x0164, "i"};
    constexpr scm::OpcodeInfo CREATE_MARKER_ABOVE_ACTOR{0x0187, "iv"};
    constexpr scm::OpcodeInfo SET_MARKER_COLOR{0x0165, "ii"};
    constexpr scm::OpcodeInfo SET_MARKER_MODE{0x018B, "ii"};
    constexpr scm::OpcodeInfo WARP_ACTOR_INTO_CAR{0x036A, "ii"};
    constexpr scm::OpcodeInfo WARP_ACTOR_INTO_CAR_AS_PASSENGER{0x0430, "iii"};
    constexpr scm::OpcodeInfo TASK_ENTER_CAR_AS_DRIVER{0x05CB, "iii"};
    constexpr scm::OpcodeInfo TASK_ENTER_CAR_AS_PASSENGER{0x05CA, "iiii"};
    constexpr scm::OpcodeInfo TASK_LEAVE_CAR{0x05CD, "ii"};
    constexpr scm::OpcodeInfo WARP_ACTOR_FROM_CAR{0x0362, "ifff"};
    constexpr scm::OpcodeInfo TOGGLE_PLAYER_CONTROLLABLE{0x01B4, "ii"};
    constexpr scm::OpcodeInfo LOCK_ACTOR{0x04D7, "ii"};
    constexpr scm::OpcodeInfo HANDS_UP{0x05C4, "ii"};
    constexpr scm::OpcodeInfo SET_WALK_STYLE{0x0245, "is"};
    constexpr scm::OpcodeInfo PERFORM_ANIMATION{0x0812, "issfiiiii"};
    constexpr scm::OpcodeInfo CLEAR_TASKS{0x0687, "i"};
    constexpr scm::OpcodeInfo SELECT_INTERIOR{0x04BB, "i"};
    constexpr scm::OpcodeInfo LINK_ACTOR_TO_INTERIOR{0x0860, "ii"};
    constexpr scm::OpcodeInfo REFRESH_RENDERER{0x04E4, "ff"};
    constexpr scm::OpcodeInfo SET_CELLPHONE{0x0729, "ii"};
    constexpr scm::OpcodeInfo CREATE_PARTICLE{0x066A, "siffffffiv"};
    constexpr scm::OpcodeInfo DESTROY_PARTICLE{0x064C, "i"};

    template <class T>
    T Read(const void* data, std::size_t offset)
    {
        T value{};
        if (data)
            std::memcpy(&value, static_cast<const std::uint8_t*>(data) + offset, sizeof(value));
        return value;
    }

    template <class T>
    void Write(void* data, std::size_t offset, const T& value)
    {
        if (data)
            std::memcpy(static_cast<std::uint8_t*>(data) + offset, &value, sizeof(value));
    }

    bool NativeValid(const void* entity)
    {
        return entity && Read<std::uintptr_t>(entity, 0) != 0x863C40;
    }

    void* PedFromHandle(std::uint32_t handle)
    {
        auto* pool = *reinterpret_cast<void**>(0xB74490);
        return pool && handle && (handle >> 8) < Read<std::uint32_t>(pool, 8) ? reinterpret_cast<void*(__thiscall*)(void*, std::uint32_t)>(0x404910)(pool, handle) : nullptr;
    }

    std::uint32_t PedHandleFromPointer(void* ped)
    {
        auto* pool = *reinterpret_cast<void**>(0xB74490);
        return pool && ped ? reinterpret_cast<std::uint32_t(__thiscall*)(void*, void*)>(0x4442D0)(pool, ped) : 0;
    }

    void* VehicleFromHandle(std::uint32_t handle)
    {
        auto* pool = *reinterpret_cast<void**>(0xB74494);
        return pool && handle && (handle >> 8) < Read<std::uint32_t>(pool, 8) ? reinterpret_cast<void*(__thiscall*)(void*, std::uint32_t)>(0x4048E0)(pool, handle) : nullptr;
    }

    std::uint32_t VehicleHandleFromPointer(void* vehicle)
    {
        auto* pool = *reinterpret_cast<void**>(0xB74494);
        return pool && vehicle ? reinterpret_cast<std::uint32_t(__thiscall*)(void*, void*)>(0x424160)(pool, vehicle) : 0;
    }

    void* LocalPed()
    {
        return *reinterpret_cast<void**>(0xB7CD98);
    }

    bool InVehicle(const void* ped)
    {
        return ped && (Read<std::uint32_t>(ped, 0x46C) & 0x100) != 0;
    }

    void SetNativeRotation(void* ped, float degrees)
    {
        if (!NativeValid(ped))
            return;
        const float radians = degrees * RADIANS;
        Write(ped, 0x55C, radians);
        Write(ped, 0x558, radians);
    }

    void NotifyPlayerPed(CPed* ped)
    {
        if (ped)
            game::players::SetPed(ped->m_nPlayerNumber, ped->m_pGamePed);
    }

    bool ValidAccessorySlot(int slot)
    {
        return slot >= 0 && slot < static_cast<int>(CPed::MAX_ACCESSORIES);
    }
}

void* CPed::GetLocalPedHandle()
{
    return reinterpret_cast<void*(__cdecl*)(int)>(0x56E210)(-1);
}

bool CPed::GetLocalPlayerPosition(CVector& result)
{
    auto* ped = GetLocalPedHandle();
    if (!ped)
        return false;

    auto* matrix = Read<void*>(ped, 0x14);
    result = matrix ? Read<CVector>(matrix, 0x30) : Read<CVector>(ped, 4);
    return true;
}

void CPed::GetNativeBonePosition(void* ped, CVector* position, unsigned int bone, bool update)
{
    if (!position || !ped)
        return;
    reinterpret_cast<void(__thiscall*)(void*, CVector*, unsigned int, bool)>(0x5E4280)(ped, position, bone, update);
}

CPed::CPed()
{
    m_handle = PedHandleFromPointer(LocalPed());
    m_pGamePed = LocalPed();
    m_pGameEntity = m_pGamePed;
    m_nPlayerNumber = 0;
    pad_2a8[0] = 1;
    pad_2a8[1] = 1;
    NotifyPlayerPed(this);
}

CPed::CPed(int playerNumber, int model, CVector position, float rotation)
{
    std::uint32_t player = static_cast<std::uint8_t>(playerNumber);
    std::uint32_t actor = 0;

    scm::ProcessCommand(&CREATE_PLAYER, &player, static_cast<double>(position.x), static_cast<double>(position.y), static_cast<double>(position.z), &actor);
    scm::ProcessCommand(&CREATE_ACTOR_FROM_PLAYER, &player, &actor);

    m_handle = actor;
    m_pGamePed = PedFromHandle(m_handle);
    m_pGameEntity = m_pGamePed;
    m_nPlayerNumber = static_cast<std::uint8_t>(playerNumber);
    pad_2a8[0] = 1;
    pad_2a8[1] = 1;
    NotifyPlayerPed(this);

    SetModelIndex(model);
    ForceRotation(rotation);

    if (NativeValid(m_pGamePed))
    {
        CMatrix matrix = CMatrix::Unity();
        GetMatrix(&matrix);
        matrix.GetPosition() = position;
        matrix.GetPosition().z += 0.15f;
        SetMatrix(matrix);
    }
}

CPed::~CPed()
{
    DeleteAllAccessories();
    DeleteArrow();
    StopUrinating();
}

void CPed::ResetPointers()
{
    const void* previous = m_pGamePed;
    if (!m_nPlayerNumber)
    {
        m_pGamePed = LocalPed();
        m_handle = PedHandleFromPointer(m_pGamePed);
    }
    else
        m_pGamePed = PedFromHandle(m_handle);
    if (previous != m_pGamePed)
        DeleteAllAccessories();
    m_pGameEntity = m_pGamePed;
}

void CPed::SetInitialState()
{
    if (NativeValid(m_pGamePed))
        reinterpret_cast<void(__thiscall*)(void*, int)>(0x60CD20)(m_pGamePed, 0);
}

std::uint32_t CPed::GetVehicleRef() const
{
    if (!NativeValid(m_pGamePed))
        return 0;
    return VehicleHandleFromPointer(Read<void*>(m_pGamePed, 0x58C));
}

void CPed::DeleteArrow()
{
    if (m_arrow)
        scm::ProcessCommand(&DISABLE_MARKER, m_arrow);
    m_arrow = 0;
}

BOOL CPed::IsOnScreen() const
{
    return NativeValid(m_pGamePed) && reinterpret_cast<bool(__thiscall*)(void*)>(0x534540)(m_pGamePed);
}

float CPed::GetHealth() const
{
    return NativeValid(m_pGamePed) ? Read<float>(m_pGamePed, 0x540) : 0.0f;
}

void CPed::SetHealth(float value)
{
    if (NativeValid(m_pGamePed))
        Write(m_pGamePed, 0x540, value);
}

float CPed::GetArmour() const
{
    return NativeValid(m_pGamePed) ? Read<float>(m_pGamePed, 0x548) : 0.0f;
}

float CPed::GetAimZ() const
{
    if (!NativeValid(m_pGamePed))
        return 0.0f;
    auto* playerData = Read<void*>(m_pGamePed, 0x480);
    return playerData ? Read<float>(playerData, 0x54) : 0.0f;
}

void CPed::SetAim(const AimStuff::Aim* aim)
{
    if (aim)
        AimStuff::SetAim(m_nPlayerNumber, aim);
}

void CPed::SetAimZ(float value)
{
    if (!NativeValid(m_pGamePed) || !std::isfinite(value) || value < -100.0f || value > 100.0f)
        return;
    if (auto* playerData = Read<void*>(m_pGamePed, 0x480))
        Write(playerData, 0x54, value);
}

void CPed::SetArmour(float value)
{
    if (NativeValid(m_pGamePed))
        Write(m_pGamePed, 0x548, value);
}

int CPed::GetFlags() const
{
    return NativeValid(m_pGamePed) ? static_cast<int>(Read<std::uint32_t>(m_pGamePed, 0x46C)) : 0;
}

void CPed::SetFlags(int value)
{
    if (NativeValid(m_pGamePed))
        Write(m_pGamePed, 0x46C, static_cast<std::uint32_t>(value));
}

BOOL CPed::IsDead() const
{
    return !NativeValid(m_pGamePed) || GetHealth() <= 0.0f;
}

char CPed::GetState() const
{
    return NativeValid(m_pGamePed) ? Read<char>(m_pGamePed, 0x530) : 0;
}

void CPed::SetState(char value)
{
    if (NativeValid(m_pGamePed))
        Write(m_pGamePed, 0x530, static_cast<std::uint32_t>(static_cast<std::uint8_t>(value)));
}

float CPed::GetRotation() const
{
    if (!NativeValid(m_pGamePed) || !PedFromHandle(m_handle))
        return 0.0f;

    CMatrix matrix = CMatrix::Unity();
    GetMatrix(&matrix);
    float result = std::atan2(-matrix.GetForward().x, matrix.GetForward().y) * DEGREES;
    if (result < 0.0f)
        result += 360.0f;
    else if (result >= 360.0f)
        result -= 360.0f;
    return result;
}

void CPed::ForceRotation(float value)
{
    if (NativeValid(m_pGamePed) && PedFromHandle(m_handle))
        SetNativeRotation(m_pGamePed, value);
}

void CPed::SetRotation(float value)
{
    if (!NativeValid(m_pGamePed) || !PedFromHandle(m_handle))
        return;
    SetNativeRotation(m_pGamePed, value);
    scm::ProcessCommand(&SET_ACTOR_Z_ANGLE, m_handle, static_cast<double>(value));
}

BOOL CPed::IsPassenger() const
{
    if (!NativeValid(m_pGamePed) || !InVehicle(m_pGamePed))
        return FALSE;

    auto* vehicle = Read<void*>(m_pGamePed, 0x58C);
    if (!vehicle)
        return FALSE;
    if (Read<void*>(vehicle, 0x460) != m_pGamePed)
        return TRUE;

    const auto model = Read<std::uint16_t>(vehicle, 0x22);
    return model == 570 || model == 569;
}

BOOL CPed::IsInVehicle() const
{
    return NativeValid(m_pGamePed) && InVehicle(m_pGamePed);
}

void* CPed::GetVehicle() const
{
    return NativeValid(m_pGamePed) ? Read<void*>(m_pGamePed, 0x58C) : nullptr;
}

float CPed::GetDistanceToEntity(const CEntity* entity) const
{
    if (!entity || !NativeValid(m_pGamePed) || !entity->IsValid())
        return 10000.0f;
    CMatrix other = CMatrix::Unity();
    entity->GetMatrix(&other);
    return GetDistanceToPoint(other.GetPosition());
}

int CPed::GetVehicleSeatIndex() const
{
    if (!NativeValid(m_pGamePed) || GetState() != 0x32)
        return -1;

    auto* vehicle = Read<void*>(m_pGamePed, 0x58C);
    if (!vehicle)
        return -1;
    if (Read<void*>(vehicle, 0x460) == m_pGamePed)
        return 0;
    for (int i = 0; i < 7; ++i)
        if (Read<void*>(vehicle, 0x464 + i * 4) == m_pGamePed)
            return i + 1;
    return -1;
}

void CPed::PutIntoVehicle(std::uint32_t vehicleRef, int seat)
{
    if (!NativeValid(m_pGamePed) || !PedFromHandle(m_handle))
        return;
    auto* vehicle = VehicleFromHandle(vehicleRef);
    if (!NativeValid(vehicle) || Read<float>(vehicle, 0x4C0) <= 0.0f)
        return;

    if (seat == 0)
        scm::ProcessCommand(&WARP_ACTOR_INTO_CAR, m_handle, vehicleRef);
    else
        scm::ProcessCommand(&WARP_ACTOR_INTO_CAR_AS_PASSENGER, m_handle, vehicleRef, seat - 1);
}

void CPed::EnterVehicle(std::uint32_t vehicleRef, BOOL asPassenger)
{
    if (!NativeValid(m_pGamePed) || !PedFromHandle(m_handle))
        return;
    auto* vehicle = VehicleFromHandle(vehicleRef);
    if (!NativeValid(vehicle))
        return;

    if (asPassenger)
    {
        if (Read<std::uint16_t>(vehicle, 0x22) == 570 && m_pGamePed == LocalPed())
            scm::ProcessCommand(&WARP_ACTOR_INTO_CAR_AS_PASSENGER, m_handle, vehicleRef, -1);
        else
            scm::ProcessCommand(&TASK_ENTER_CAR_AS_PASSENGER, m_handle, vehicleRef, 3000, -1);
    }
    else
    {
        scm::ProcessCommand(&TASK_ENTER_CAR_AS_DRIVER, m_handle, vehicleRef, 3000);
    }
}

void CPed::ExitVehicle()
{
    if (!NativeValid(m_pGamePed) || !InVehicle(m_pGamePed))
        return;
    auto* vehicle = Read<void*>(m_pGamePed, 0x58C);
    if (!vehicle)
        return;
    const auto model = Read<std::uint16_t>(vehicle, 0x22);
    if (model == 570 || model == 538)
        return;
    const auto ref = VehicleHandleFromPointer(vehicle);
    if (ref)
        scm::ProcessCommand(&TASK_LEAVE_CAR, m_handle, ref);
}

void CPed::WarpFromVehicle(CVector putAt)
{
    if (NativeValid(m_pGamePed) && InVehicle(m_pGamePed))
        scm::ProcessCommand(&WARP_ACTOR_FROM_CAR, m_handle, static_cast<double>(putAt.x), static_cast<double>(putAt.y), static_cast<double>(putAt.z));
}

void CPed::SetSpawnInfo(const CVector* position, float rotation)
{
    if (!position)
        return;
    *reinterpret_cast<std::uint16_t*>(0xA4326C) = 0;
    *reinterpret_cast<CVector*>(0xA43318) = *position;
    *reinterpret_cast<float*>(0xA432E8) = rotation;
    *reinterpret_cast<std::uint32_t*>(0xA432C0) = 0;
}

void CPed::SetControllable(BOOL enable)
{
    if (!PedFromHandle(m_handle))
        return;

    scm::ProcessCommand(&TOGGLE_PLAYER_CONTROLLABLE, static_cast<int>(m_nPlayerNumber), enable ? 1 : 0);
    scm::ProcessCommand(&LOCK_ACTOR, m_handle, enable ? 0 : 1);

    if (enable && NativeValid(m_pGamePed) && !InVehicle(m_pGamePed))
    {
        CMatrix matrix = CMatrix::Unity();
        GetMatrix(&matrix);
        Teleport(matrix.GetPosition());
    }
}

void CPed::ResetKeys()
{
    KeyStuff::ResetKeys(m_nPlayerNumber);
}

short CPed::GetKeys(short* leftStickX, short* leftStickY) const
{
    if (!leftStickX || !leftStickY)
        return 0;

    CPad pad{};
    const auto* internalKeys = KeyStuff::GetInternalKeys();
    if (!internalKeys)
    {
        *leftStickX = 0;
        *leftStickY = 0;
        return 0;
    }

    std::memcpy(&pad, internalKeys, sizeof(pad));
    const auto& keys = pad.NewState;
    *leftStickX = keys.LeftStickX;
    *leftStickY = keys.LeftStickY;

    ControllerState state{};
    // R5's packed right-stick pairs encode positive as value 2 and negative as value 1.
    state.m_bRightStickLeft = keys.RightStickX > 0;
    state.m_bRightStickRight = keys.RightStickX < 0;
    state.m_bRightStickUp = keys.RightStickY > 0;
    state.m_bRightStickDown = keys.RightStickY < 0;
    state.m_bPedWalk = keys.m_bPedWalk != 0;
    state.m_bShockButtonR = keys.ShockButtonR != 0;

    const bool inVehicle = m_pGamePed && InVehicle(m_pGamePed);
    state.m_bLeftShoulder2 = inVehicle && keys.LeftShoulder2 != 0;
    state.m_bRightShoulder1 = keys.RightShoulder1 != 0;
    state.m_bRightShoulder2 = inVehicle && keys.RightShoulder2 != 0;
    state.m_bButtonSquare = keys.ButtonSquare != 0;

    bool suppressEnter = false;
    if (m_pGamePed && !inVehicle)
    {
        auto* intelligence = Read<void*>(m_pGamePed, 0x47C);
        auto* task = intelligence ? Read<void*>(intelligence, 0x10) : nullptr;
        suppressEnter = task && Read<std::uintptr_t>(task, 0) == 0x8705C4;
    }
    state.m_bButtonTriangle = !suppressEnter && keys.ButtonTriangle != 0;
    state.m_bButtonCross = keys.ButtonCross != 0;

    bool suppressFire = false;
    if (m_pGamePed)
    {
        const auto slot = Read<std::uint8_t>(m_pGamePed, 0x718);
        const auto* weapon = static_cast<const std::uint8_t*>(m_pGamePed) + 0x5A0 + slot * 0x1C;
        const int type = Read<int>(weapon, 0);
        suppressFire = type > 15 && type != 46 && Read<int>(weapon, 0x0C) == 0;
    }
    state.m_bButtonCircle = !suppressFire && keys.ButtonCircle != 0;
    state.m_bShockButtonL = keys.ShockButtonL != 0;
    state.m_bLeftShoulder1 = keys.LeftShoulder1 != 0;
    return state.m_value;
}

void CPed::SetKeys(short buttons, short leftStickX, short leftStickY)
{
    bool suppressEnter = false;
    if (NativeValid(m_pGamePed) && !InVehicle(m_pGamePed))
    {
        auto* intelligence = Read<void*>(m_pGamePed, 0x47C);
        auto* task = intelligence ? Read<void*>(intelligence, 0x10) : nullptr;
        suppressEnter = task && Read<std::uintptr_t>(task, 0) == 0x8705C4;
    }
    auto* pad = KeyStuff::GetKeys(m_nPlayerNumber);
    if (!pad)
        return;

    auto axis = [](std::int16_t& current, std::int16_t& previous, short value) {
        previous = current && value ? value : 0;
        current = value;
    };
    auto button = [](std::int16_t& current, std::int16_t& previous, bool down) {
        previous = down && current ? 255 : 0;
        current = down ? 255 : 0;
    };
    auto buttonExact255 = [](std::int16_t& current, std::int16_t& previous, bool down) {
        previous = down && current == 255 ? 255 : 0;
        current = down ? 255 : 0;
    };

    auto& now = pad->NewState;
    auto& old = pad->OldState;
    const auto bits = static_cast<std::uint16_t>(buttons);
    const bool inVehicle = NativeValid(m_pGamePed) && InVehicle(m_pGamePed);
    axis(now.LeftStickX, old.LeftStickX, leftStickX);
    axis(now.LeftStickY, old.LeftStickY, leftStickY);
    button(now.LeftShoulder1, old.LeftShoulder1, bits & 1);
    button(now.ShockButtonL, old.ShockButtonL, inVehicle && (bits & 2));
    button(now.ButtonCircle, old.ButtonCircle, bits & 4);
    button(now.ButtonCross, old.ButtonCross, bits & 8);
    button(now.ButtonTriangle, old.ButtonTriangle, !suppressEnter && (bits & 16));
    button(now.ButtonSquare, old.ButtonSquare, bits & 32);
    button(now.RightShoulder2, old.RightShoulder2, bits & 64);
    button(now.RightShoulder1, old.RightShoulder1, bits & 128);
    button(now.LeftShoulder2, old.LeftShoulder2, bits & 256);
    buttonExact255(now.ShockButtonR, old.ShockButtonR, bits & 512);
    buttonExact255(now.m_bPedWalk, old.m_bPedWalk, bits & 1024);
    const auto lookY = (bits >> 11) & 3;
    const auto lookX = (bits >> 13) & 3;
    now.RightStickY = old.RightStickY = lookY == 2 ? 128 : lookY == 1 ? -128 : 0;
    now.RightStickX = old.RightStickX = lookX == 2 ? 128 : lookX == 1 ? -128 : 0;
}

void CPed::SetPlayerStat(int index, std::uint16_t value)
{
    if (m_nPlayerNumber == 0)
        AimStuff::SetLocalPlayerStat(index, value);
    else
        AimStuff::SetRemotePlayerStat(m_nPlayerNumber, index, value);
}

void* CPed::GetFloor() const
{
    return IsOnGround() ? GetContactEntity() : nullptr;
}

void CPed::HandsUp()
{
    if (NativeValid(m_pGamePed) && !InVehicle(m_pGamePed) && DoesExist() && PedFromHandle(m_handle))
        scm::ProcessCommand(&HANDS_UP, m_handle, -2);
}

BOOL CPed::DoesHandsUp() const
{
    if (!NativeValid(m_pGamePed) || InVehicle(m_pGamePed) || !DoesExist() || !PedFromHandle(m_handle))
        return FALSE;
    auto* taskManager = Read<void*>(m_pGamePed, 0x47C);
    auto* task = taskManager ? Read<void*>(taskManager, 0x10) : nullptr;
    return task && Read<std::uintptr_t>(task, 0) == 0x85A29C;
}

void CPed::HoldObject(int model)
{
    if (NativeValid(m_pGamePed) && DoesExist() && PedFromHandle(m_handle))
        reinterpret_cast<void(__thiscall*)(void*, int, int)>(0x5E4390)(m_pGamePed, model, 1);
}

void CPed::Extinguish()
{
    if (!NativeValid(m_pGamePed))
        return;
    if (auto* fire = Read<void*>(m_pGamePed, 0x730))
        reinterpret_cast<void(__thiscall*)(void*)>(0x5393F0)(fire);
    if (InVehicle(m_pGamePed))
    {
        auto* vehicle = Read<void*>(m_pGamePed, 0x58C);
        if (vehicle)
            if (auto* fire = Read<void*>(vehicle, 0x490))
                reinterpret_cast<void(__thiscall*)(void*)>(0x5393F0)(fire);
    }
}

void CPed::SetWalkStyle(const char* name)
{
    if (NativeValid(m_pGamePed) && name)
        scm::ProcessCommand(&SET_WALK_STYLE, m_handle, name);
}

void CPed::SetFightingStyle(int style)
{
    if (!NativeValid(m_pGamePed))
        return;

    // same set the game accepts: standard, boxing, kung fu, knee head, grab kick, elbows
    switch (style)
    {
        case 4:
        case 5:
        case 6:
        case 7:
        case 15:
        case 16:
            break;
        default:
            style = 4;
            break;
    }

    // 0x72D style, 0x72E allowed moves (6 = what the scm command uses)
    Write<std::uint8_t>(m_pGamePed, 0x72D, static_cast<std::uint8_t>(style));
    Write<std::uint8_t>(m_pGamePed, 0x72E, 6);
}

int CPed::GetFightingStyle() const
{
    return NativeValid(m_pGamePed) ? Read<std::uint8_t>(m_pGamePed, 0x72D) : 0;
}

void CPed::PerformAnimation(const char* name, const char* file, float frameDelta, int loop, int lockX, int lockY, int lockF, int time)
{
    if (!NativeValid(m_pGamePed) || !PedFromHandle(m_handle) || !name || !file)
        return;
    scm::ProcessCommand(&PERFORM_ANIMATION, m_handle, name, file, static_cast<double>(frameDelta), loop, lockX, lockY, lockF, time);
}

void CPed::ClearTasks()
{
    if (!NativeValid(m_pGamePed) || !PedFromHandle(m_handle))
        return;
    scm::ProcessCommand(&CLEAR_TASKS, m_handle);
}

void CPed::LinkToInterior(char id, BOOL refresh)
{
    if (!NativeValid(m_pGamePed))
        return;
    const int interior = static_cast<std::uint8_t>(id);
    if (m_nPlayerNumber == 0)
    {
        scm::ProcessCommand(&SELECT_INTERIOR, interior);
        scm::ProcessCommand(&LINK_ACTOR_TO_INTERIOR, m_handle, interior);
        if (refresh)
        {
            CMatrix matrix = CMatrix::Unity();
            GetMatrix(&matrix);
            scm::ProcessCommand(&REFRESH_RENDERER, static_cast<double>(matrix.GetPosition().x), static_cast<double>(matrix.GetPosition().y));
        }
    }
    else
    {
        scm::ProcessCommand(&LINK_ACTOR_TO_INTERIOR, m_handle, interior);
    }
}

BOOL CPed::IsOnGround() const
{
    return NativeValid(m_pGamePed) && (Read<std::uint8_t>(m_pGamePed, 0x46C) & 3) != 0;
}

void CPed::ResetDamageEntity()
{
    if (!NativeValid(m_pGamePed))
        return;
    Write<std::uint32_t>(m_pGamePed, 0x764, 0);
    Write<std::uint32_t>(m_pGamePed, 0x760, 0xFF);
}

void* CPed::GetContactEntity() const
{
    return NativeValid(m_pGamePed) ? Read<void*>(m_pGamePed, 0x584) : nullptr;
}

void* CPed::GetContactVehicle() const
{
    return NativeValid(m_pGamePed) ? Read<void*>(m_pGamePed, 0x568) : nullptr;
}

int CPed::GetStat() const
{
    if (!NativeValid(m_pGamePed))
        return -1;

    return ModelInfo::GetPedStat(Read<std::uint16_t>(m_pGamePed, 0x22));
}

BOOL CPed::PerformingCustomAnimation() const
{
    if (!NativeValid(m_pGamePed) || !DoesExist())
        return FALSE;
    auto* taskManager = Read<void*>(m_pGamePed, 0x47C);
    return taskManager && Read<void*>(taskManager, 0x10) != nullptr;
}

void CPed::StartDancing(int style)
{
    if (style < 0 || style > 3)
        return;
    m_bDoesDancing = TRUE;
    m_nDanceStyle = static_cast<std::uint32_t>(style);

    if (m_nPlayerNumber == 0)
    {
        static constexpr const char* libraries[] = {"WOP", "GFUNK", "RUNNINGMAN", "STRIP"};
        static constexpr const char* animations[] = {"DANCE_LOOP", "DANCE_LOOP", "DANCE_LOOP", "STR_Loop_B"};
        PerformAnimation(animations[style], libraries[style], 16.0f, 1, 0, 0, 0, -1);
    }
}

void CPed::StopDancing()
{
    m_bDoesDancing = FALSE;
    if (NativeValid(m_pGamePed))
    {
        CMatrix matrix = CMatrix::Unity();
        GetMatrix(&matrix);
        Teleport(matrix.GetPosition());
    }
}

BOOL CPed::DoesDancing() const
{
    return m_bDoesDancing != FALSE;
}

const char* CPed::GetAnimationForDance(int move) const
{
    if (!m_bDoesDancing || move < 0 || move > 3)
        return nullptr;
    static constexpr const char* animations[] = {"DANCE_B1", "DANCE_B2", "DANCE_B3", "DANCE_B4"};
    return animations[move];
}

void CPed::ProcessDancing()
{
    if (!m_bDoesDancing)
        return;
    if (m_nLastDanceMove > 3)
        m_nLastDanceMove = 0;
}

void CPed::LookAt(const CVector& target)
{
    if (!m_pGamePed || !std::isfinite(target.x) || !std::isfinite(target.y) || !std::isfinite(target.z))
        return;
    using LookAtFn = void(__thiscall*)(void*, const char*, void*, void*, int, int, const CVector*, bool, float, int, int, bool);
    reinterpret_cast<LookAtFn>(0x618970)(reinterpret_cast<void*>(0xC15448), "FollowPedSA", m_pGamePed, nullptr, 2000, -1, &target, false, 0.1f, 500, 3, false);

    auto* intelligence = Read<std::uint8_t*>(m_pGamePed, 0x47C);
    if (!intelligence)
        return;
    auto* manager = reinterpret_cast<void*(__thiscall*)(void*, int)>(0x681810)(intelligence + 4, 5);
    auto* task = Read<void*>(manager, 8);
    if (!task || Read<std::int8_t>(task, 0x59) > 3)
        return;

    // GTA updates the task speed but leaves an existing chain at its old speed
    // keep the active chain at the same 0.1 speed requested by R5
    if (auto* chain = Read<void*>(task, 0x10))
        Write(chain, 0x44, 0.1f);
}

void CPed::StopLooking()
{
    auto* intelligence = Read<std::uint8_t*>(m_pGamePed, 0x47C);
    if (!intelligence)
        return;

    // vanilla AbortLookAt dereferences the IK manager even when no task exists
    auto* manager = reinterpret_cast<void*(__thiscall*)(void*, int)>(0x681810)(intelligence + 4, 5);
    if (!manager || !Read<void*>(manager, 8))
        return;

    reinterpret_cast<void(__thiscall*)(void*, void*, unsigned int)>(0x618280)(reinterpret_cast<void*>(0xC15448), m_pGamePed, 250);
}

void CPed::ApplyCommandTask(const char* name, int a3, int a4, int a5, int a6, int a7, int a8, int a9, int a10, int a11)
{
    if (!NativeValid(m_pGamePed) || !name || !*name)
        return;

    using ApplyTaskFn = void(__thiscall*)(void*, const char*, void*, int, int, int, int, int, int, int, int, int);
    reinterpret_cast<ApplyTaskFn>(0x618970)(reinterpret_cast<void*>(0xC15448), name, m_pGamePed, a3, a4, a5, a6, a7, a8, a9, a10, a11);
}

void CPed::DestroyCommandTask()
{
    if (!NativeValid(m_pGamePed))
        return;

    auto* intelligence = Read<std::uint8_t*>(m_pGamePed, 0x47C);
    if (!intelligence)
        return;

    // r5 clears the secondary IK task, not the primary task at +0x10
    auto* task = Read<void*>(intelligence, 0x2C);
    if (!task)
        return;

    reinterpret_cast<void(__thiscall*)(void*, int)>(0x639330)(task, 1);
    Write<void*>(intelligence, 0x2C, nullptr);
}

void CPed::EnableCellphone(BOOL enable)
{
    if (!NativeValid(m_pGamePed))
        return;
    scm::ProcessCommand(&SET_CELLPHONE, m_handle, enable ? 1 : 0);
    m_bUsingCellphone = enable ? TRUE : FALSE;
}

BOOL CPed::UsingCellphone() const
{
    return m_bUsingCellphone != FALSE;
}

void CPed::StartUrinating()
{
    if (!NativeValid(m_pGamePed) || m_bDoesUrinating)
        return;

    CVector position{};
    GetBonePosition(2, &position);
    std::uint32_t particle = 0;
    scm::ProcessCommand(&CREATE_PARTICLE, "water_hydrant", 0, static_cast<double>(position.x), static_cast<double>(position.y), static_cast<double>(position.z), 0.0, 0.0, 0.0, 0,
                        &particle);
    m_urinatingParticle = particle;
    m_bDoesUrinating = TRUE;
}

void CPed::StopUrinating()
{
    if (m_urinatingParticle)
        scm::ProcessCommand(&DESTROY_PARTICLE, m_urinatingParticle);
    m_urinatingParticle = 0;
    m_bDoesUrinating = FALSE;
}

BOOL CPed::DoesUrinating() const
{
    return m_bDoesUrinating != FALSE;
}

const char* CPed::GetLoadedShoppingDataSubsection() const
{
    return reinterpret_cast<const char*>(0xA9A7D8);
}

void CPed::LoadShoppingDataSubsection(const char* name)
{
    if (!name || !*name)
        return;
    reinterpret_cast<void(__cdecl*)(const char*)>(0x49BBE0)(name);
}

void CPed::CreateArrow(int color)
{
    DeleteArrow();
    if (!m_handle)
        return;
    scm::ProcessCommand(&CREATE_MARKER_ABOVE_ACTOR, m_handle, &m_arrow);
    if (!m_arrow)
        return;
    scm::ProcessCommand(&SET_MARKER_COLOR, m_arrow, color);
    scm::ProcessCommand(&SET_MARKER_MODE, m_arrow, 2);
}

void CPed::SetModelIndex(int model)
{
    if (!NativeValid(m_pGamePed) || !PedFromHandle(m_handle))
        return;
    auto* info = ModelInfo::GetRelocated(model);
    if (!info || Read<std::uintptr_t>(info, 0) != 0x85BDC0)
        model = 0;
    // keep the assembled CJ template resident even while all players use other skins
    reinterpret_cast<void(__cdecl*)(int, int)>(0x4087E0)(0, 2);
    // r5 disables clothes rebuilding before changing player skins: it replaces
    // the shared model clump and invalidates other player peds using that skin
    if (!patch::write<std::uint8_t>(0x5A82C0, 0xC3))
    {
        Log("[game:ped] failed to disable clothes rebuilding");
        ExitProcess(1);
    }
    DestroyCommandTask();
    CEntity::SetModelIndex(model);
    reinterpret_cast<void(__thiscall*)(void*, void*)>(0x4E68D0)(static_cast<std::uint8_t*>(m_pGamePed) + 0x294, m_pGamePed);
}

BOOL CPed::CopyAppearanceFrom(const CPed& source)
{
    if (!NativeValid(m_pGamePed) || !NativeValid(source.m_pGamePed) || !PedFromHandle(m_handle) || !PedFromHandle(source.m_handle))
        return FALSE;
    const int model = source.GetModelIndex();
    SetModelIndex(model);
    if (GetModelIndex() != model)
        return FALSE;
    // native CPed movement animation group and fighting style; never share clump associations or tasks
    Write<std::uint32_t>(m_pGamePed, 0x4D4, Read<std::uint32_t>(source.m_pGamePed, 0x4D4));
    Write<std::uint8_t>(m_pGamePed, 0x72C, Read<std::uint8_t>(source.m_pGamePed, 0x72C));
    Write<std::uint8_t>(m_pGamePed, 0x72D, Read<std::uint8_t>(source.m_pGamePed, 0x72D));
    return TRUE;
}

void CPed::Destroy()
{
    game::players::SetPed(m_nPlayerNumber, nullptr);

    DeleteArrow();
    StopUrinating();
    DeleteAllAccessories();
    DestroyCommandTask();

    if (NativeValid(m_pGamePed) && PedFromHandle(m_handle))
    {
        if (InVehicle(m_pGamePed))
            WarpFromVehicle({100.0f, 100.0f, 10.0f});

        auto* intelligence = Read<void*>(m_pGamePed, 0x480);
        if (intelligence)
            Write<std::uint32_t>(intelligence, 0x4C, 0);

        auto* vtable = Read<void**>(m_pGamePed, 0);
        if (vtable && vtable[0])
            reinterpret_cast<void(__thiscall*)(void*, int)>(vtable[0])(m_pGamePed, 1);
    }

    m_pGamePed = nullptr;
    m_pGameEntity = nullptr;
    m_handle = 0;
    game::players::ReleaseSlot(m_nPlayerNumber);
}

void CPed::SetCameraMode(char mode)
{
    AimStuff::SetCameraMode(mode, m_nPlayerNumber);
}

void CPed::SetCameraExtZoomAndAspectRatio(float extZoom, float aspectRatio)
{
    AimStuff::SetCameraExtZoomAndAspectRatio(m_nPlayerNumber, extZoom, aspectRatio);
}

char CPed::GetCameraMode() const
{
    return AimStuff::GetCameraMode(m_nPlayerNumber);
}

BOOL CPed::HasAccessory() const
{
    for (BOOL state : m_accessories.m_bNotEmpty)
        if (state != FALSE)
            return TRUE;
    return FALSE;
}

void CPed::DeleteAccessory(int slot)
{
    if (!ValidAccessorySlot(slot) || m_accessories.m_bNotEmpty[slot] == FALSE)
        return;
    auto* object = m_accessories.m_pObject[slot];
    m_accessories.m_pObject[slot] = nullptr;
    m_accessories.m_info[slot] = {};
    m_accessories.m_bNotEmpty[slot] = FALSE;
    delete object;
}

BOOL CPed::GetAccessoryState(int slot) const
{
    return ValidAccessorySlot(slot) && m_accessories.m_bNotEmpty[slot] != FALSE;
}

void CPed::DeleteAllAccessories()
{
    for (int i = 0; i < static_cast<int>(MAX_ACCESSORIES); ++i)
        DeleteAccessory(i);
}

void CPed::AddAccessory(int slot, const Accessory* info)
{
    if (!NativeValid(m_pGamePed) || !info || !ValidAccessorySlot(slot) || info->m_nBone <= 0 || info->m_nBone > 18)
        return;

    const Accessory value = *info;
    CMatrix validated = CMatrix::Unity();
    if (!BuildAccessoryMatrix(validated, value, &validated))
        return;
    DeleteAccessory(slot);
    m_accessories.m_info[slot] = value;
    info = &value;

    CMatrix matrix = CMatrix::Unity();
    GetMatrix(&matrix);
    auto* object = new (std::nothrow) CObject(info->m_nModel, matrix.GetPosition(), info->m_rotation, 200.0f, 1, 0, 0);
    m_accessories.m_pObject[slot] = object;
    if (!object || !object->IsValid())
    {
        delete object;
        m_accessories.m_pObject[slot] = nullptr;
        m_accessories.m_info[slot] = {};
        return;
    }

    if (info->m_firstMaterialColor)
        object->SetMaterial(-1, 0, "", "", info->m_firstMaterialColor);
    if (info->m_secondMaterialColor)
        object->SetMaterial(-1, 1, "", "", info->m_secondMaterialColor);
    object->SetCollisionFlag(FALSE);
    object->SetPoolManaged(true);
    m_accessories.m_bNotEmpty[slot] = TRUE;
}

bool CPed::GetAccessoryMatrix(int slot, CMatrix* matrix) const
{
    if (!matrix || !GetAccessoryState(slot) || !NativeValid(m_pGamePed))
        return false;
    auto* clump = Read<RpClump*>(m_pGamePed, 0x18);
    const auto& info = m_accessories.m_info[slot];
    if (!clump || info.m_nBone <= 0 || info.m_nBone > 18)
        return false;
    // r5 indexes GTA ped nodes first, then resolves the node's bone tag in the hierarchy
    auto* node = Read<void*>(m_pGamePed, 0x488 + info.m_nBone * sizeof(void*));
    if (!node)
        return false;
    auto* hierarchy = reinterpret_cast<RpHAnimHierarchy*(__cdecl*)(RpClump*)>(0x734A40)(clump);
    if (!hierarchy || hierarchy->numNodes <= 0)
        return false;
    const int index = RpHAnimIDGetIndex(hierarchy, Read<int>(node, 0x14));
    auto* matrices = RpHAnimHierarchyGetMatrixArray(hierarchy);
    if (!matrices || index < 0 || index >= hierarchy->numNodes)
        return false;
    const auto& rw = matrices[index];
    CMatrix bone = CMatrix::Unity();
    bone.GetRight() = rw.right;
    bone.GetForward() = rw.up;
    bone.GetUp() = rw.at;
    bone.GetPosition() = rw.pos;
    return BuildAccessoryMatrix(bone, info, matrix);
}

bool CPed::UpdateAccessory(int slot, const Accessory& info)
{
    if (!GetAccessoryState(slot) || !GetAccessory(slot))
        return false;
    const auto& current = m_accessories.m_info[slot];
    if (current.m_nModel != info.m_nModel || current.m_nBone != info.m_nBone)
        return false;
    CMatrix validated = CMatrix::Unity();
    if (!BuildAccessoryMatrix(validated, info, &validated))
        return false;
    m_accessories.m_info[slot] = info;
    return true;
}

void CPed::ProcessAccessories()
{
    if (!HasAccessory() || !NativeValid(m_pGamePed) || !Read<void*>(m_pGamePed, 0x18))
        return;
    reinterpret_cast<void(__thiscall*)(void*)>(0x532B20)(m_pGamePed);
    for (int slot = 0; slot < static_cast<int>(MAX_ACCESSORIES); ++slot)
    {
        auto* object = GetAccessory(slot);
        CMatrix matrix = CMatrix::Unity();
        if (!object || !object->IsValid() || !GetAccessoryMatrix(slot, &matrix))
            continue;
        object->SetMatrix(matrix);
        object->UpdateRwFrame();
        object->Process(0);
        if (object->m_bReadyToRender)
            object->Draw();
    }
}

CObject* CPed::GetAccessory(int slot) const
{
    return ValidAccessorySlot(slot) ? m_accessories.m_pObject[slot] : nullptr;
}

void CPed::GetBonePosition(unsigned int boneId, CVector* outPosition) const
{
    if (!outPosition)
        return;
    *outPosition = CVector{};
    if (!NativeValid(m_pGamePed))
        return;
    GetNativeBonePosition(m_pGamePed, outPosition, boneId, false);
}

void CPed::ClearWeapons()
{
    if (IsValid())
        reinterpret_cast<void(__thiscall*)(void*)>(0x5E6320)(m_pGamePed);
}

void CPed::GiveWeapon(int weapon, int ammo)
{
    static constexpr int models[] = {0, 331, 333, 334, 335, 336, 337, 338, 339, 341, 321, 322, 323, 324, 325, 326, 342, 343, 344, -1, -1, -1, 346, 347, 348, 349, 350, 351, 352, 353, 355, 356, 372, 357, 358, 359, 360, 361, 362, 363, 364, 365, 366, 367, 368, 369, 371};
    if (!IsValid() || !g_game || weapon < 0 || weapon >= 47 || models[weapon] < 0 || ammo < 0)
        return;
    if (models[weapon] > 0)
    {
        g_game->RequestModel(models[weapon]);
        g_game->LoadRequestedModels();
        if (!g_game->IsModelAvailable(models[weapon]))
            return;
    }
    constexpr scm::OpcodeInfo give{0x01B2, "iii"};
    scm::ProcessCommand(&give, m_handle, weapon, ammo);
    SetArmedWeapon(weapon);
}

void CPed::SetWeaponAmmo(int weapon, int ammo)
{
    if (!IsValid() || weapon < 0 || weapon > 46 || ammo < 0)
        return;
    for (int slot = 0; slot < 13; ++slot)
    {
        auto* entry = static_cast<std::uint8_t*>(m_pGamePed) + 0x5A0 + slot * 0x1C;
        if (Read<int>(entry, 0) != weapon)
            continue;
        Write(entry, 0x0C, ammo);
        if (Read<int>(entry, 0x08) > ammo)
            Write(entry, 0x08, ammo);
        return;
    }
}

void CPed::SetArmedWeapon(int weapon)
{
    if (!IsValid() || weapon < 0 || weapon > 46)
        return;
    constexpr scm::OpcodeInfo armed{0x01B9, "ii"};
    scm::ProcessCommand(&armed, m_handle, weapon);
}

std::uint8_t CPed::GetCurrentWeapon() const
{
    if (!IsValid())
        return 0;
    const auto slot = Read<std::uint8_t>(m_pGamePed, 0x718);
    if (slot >= 13)
        return 0;
    const auto weapon = Read<std::uint32_t>(m_pGamePed, 0x5A0 + slot * 0x1C);
    return weapon <= 63 ? static_cast<std::uint8_t>(weapon) : 0;
}
