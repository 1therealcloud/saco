#include <stdafx.h>

#include "CEntity.h"
#include "CPed.h"

#include "game/ModelInfo.h"
#include "game/Scripting.h"
#include "net/CNetGame.h"
#include "pools/CPlayerPool.h"
#include "players/CLocalPlayer.h"
#include "game/CGame.h"
#include "game/CCamera.h"
#include "gui/CChat.h"
#include "main.h"

#include <bit>
#include <cmath>

namespace
{
    constexpr scm::OpcodeInfo SET_TRAIN_COORDINATES{0x07C7, "ifff"};
    constexpr float MISSING_ENTITY_DISTANCE = 100000.0f;

    template <class T>
    T Read(const void* data, std::size_t offset)
    {
        T value;
        std::memcpy(&value, static_cast<const std::uint8_t*>(data) + offset, sizeof(value));
        return value;
    }
    template <class T>
    void Write(void* data, std::size_t offset, const T& value)
    {
        std::memcpy(static_cast<std::uint8_t*>(data) + offset, &value, sizeof(value));
    }
    template <class Function>
    Function Virtual(void* entity, std::size_t offset)
    {
        return Read<Function>(Read<void*>(entity, 0), offset);
    }
    bool ReadWorldPosition(const CEntity* entity, CVector& position)
    {
        ZoneScoped;
        if (!entity || !entity->IsValid())
            return false;

        const auto* native = entity->GetNativeHandle();
        if (!Read<void*>(native, 0x14))
            return false;

        CMatrix matrix = CMatrix::Unity();
        entity->GetMatrix(&matrix);
        position = matrix.GetPosition();
        return true;
    }

    bool LocalPosition(CVector& position)
    {
        ZoneScoped;
        auto* game = g_game.get();
        auto* ped = game ? game->GetPlayerPed() : nullptr;
        if (!ped)
            return false;

        auto* pools = g_netGame ? g_netGame->m_pPools : nullptr;
        auto* localPlayer = pools && pools->m_pPlayer ? pools->m_pPlayer->GetLocalPlayer() : nullptr;

        // r5 checks CLocalPlayer +0x108 and +0x1d0 before choosing the camera.
        if (localPlayer && (localPlayer->m_bDoesSpectating || localPlayer->m_bInRCMode))
        {
            if (auto* camera = game->GetCamera())
            {
                CMatrix matrix = CMatrix::Unity();
                camera->GetMatrix(&matrix);
                position = matrix.GetPosition();
                return true;
            }
        }

        return ReadWorldPosition(ped, position);
    }

    float DistanceBetween(const CVector& a, const CVector& b, bool ignoreHeight = false)
    {
        ZoneScoped;
        const float x = a.x - b.x;
        const float y = a.y - b.y;
        if (ignoreHeight)
            return std::sqrt(x * x + y * y);

        const float z = a.z - b.z;
        return std::sqrt(x * x + y * y + z * z);
    }

    static const float R5_RADIANS_TO_DEGREES = 57.2957763671875f;
    static const float R5_INVERT_EULER = -1.0f;

    void InvertEulerR5(float* angle)
    {
        ZoneScoped;
        __asm
        {
            mov eax, angle
            fld dword ptr [eax]
            fmul dword ptr [R5_RADIANS_TO_DEGREES]
            fmul dword ptr [R5_INVERT_EULER]
            fstp dword ptr [eax]
        }
    }
}

bool CEntity::IsValid() const
{
    return m_pGameEntity && Read<std::uintptr_t>(m_pGameEntity, 0) != 0x863C40;
}

bool CEntity::IsVisible() const
{
    return m_pGameEntity && reinterpret_cast<bool(__thiscall*)(void*)>(0x534540)(m_pGameEntity);
}

void CEntity::GetMatrix(CMatrix* matrix) const
{
    ZoneScoped;
    if (!matrix || !IsValid())
        return;
    auto* native = Read<void*>(m_pGameEntity, 0x14);
    if (!native)
        return;
    matrix->GetRight() = Read<CVector>(native, 0);
    matrix->GetForward() = Read<CVector>(native, 0x10);
    matrix->GetUp() = Read<CVector>(native, 0x20);
    matrix->GetPosition() = Read<CVector>(native, 0x30);
}

void CEntity::SetMatrix(const CMatrix& matrix)
{
    ZoneScoped;
    if (!IsValid())
        return;
    auto* native = Read<void*>(m_pGameEntity, 0x14);
    if (!native)
        return;
    Write(native, 0, matrix.GetRight());
    Write(native, 0x10, matrix.GetForward());
    Write(native, 0x20, matrix.GetUp());
    Write(native, 0x30, matrix.GetPosition());
}

void CEntity::GetSpeed(CVector* speed) const
{
    if (speed)
        *speed = IsValid() ? Read<CVector>(m_pGameEntity, 0x44) : CVector{};
}
void CEntity::SetSpeed(CVector speed)
{
    if (IsValid())
        Write(m_pGameEntity, 0x44, speed);
}
void CEntity::GetTurnSpeed(CVector* speed) const
{
    if (speed)
        *speed = IsValid() ? Read<CVector>(m_pGameEntity, 0x50) : CVector{};
}
void CEntity::SetTurnSpeed(CVector speed)
{
    if (IsValid())
        Write(m_pGameEntity, 0x50, speed);
}
void CEntity::ApplyTurnSpeed()
{
    if (IsValid())
        reinterpret_cast<void(__thiscall*)(void*)>(0x542E20)(m_pGameEntity);
}
float CEntity::GetDistanceFromCentreOfMassToBaseOfModel() const
{
    if (!IsValid())
        return 0.0f;

    // gta 1.0 us returns the float bits in eax here, as used by samp r5.
    const auto bits = reinterpret_cast<std::uint32_t(__thiscall*)(void*)>(0x536BE0)(m_pGameEntity);
    return std::bit_cast<float>(bits);
}
bool CEntity::IntersectsCameraRay(const CVector& origin, const CVector& target, float scale) const
{
    ZoneScoped;
    if (!IsValid())
        return false;
    const auto* info = ModelInfo::GetRelocated(GetModelIndex());
    const auto* collision = info ? Read<void*>(info, 0x14) : nullptr;
    if (!collision)
        return false;
    float radius = Read<float>(collision, 0x24);
    if (radius <= 1.0f)
        radius *= 1.5f;
    radius *= scale;
    if (!std::isfinite(radius) || radius <= 0.0f)
        return false;
    CVector centre{};
    GetBoundCentre(&centre);
    struct Line
    {
        CVector start;
        float startSize;
        CVector end;
        float endSize;
    };
    struct Sphere
    {
        CVector centre;
        float radius;
    };
    const Line line{origin - centre, 0.0f, target - centre, 0.0f};
    const Sphere sphere{CVector{}, radius};
    return reinterpret_cast<bool(__cdecl*)(const Line&, const Sphere&)>(0x417470)(line, sphere);
}

void CEntity::GetBoundCentre(CVector* centre) const
{
    if (centre && IsValid())
        reinterpret_cast<CVector*(__thiscall*)(void*, CVector*)>(0x534250)(m_pGameEntity, centre);
}
void CEntity::SetModelIndex(int model)
{
    ZoneScoped;
    if (!IsValid() || !g_game || model < 0 || model >= ModelInfo::MAX_MODELS)
        return;

    if (!g_game->IsModelAvailable(model))
    {
        // r5's 0xb45a0 reads the rw object at modelinfo +0x1c.
        const auto* info = ModelInfo::GetRelocated(model);
        if (!info)
            return;

        if (!Read<void*>(info, 0x1C))
        {
            g_game->RequestModel(model, 2);
            g_game->LoadRequestedModels();

            if (!g_game->IsModelAvailable(model))
            {
                int attempts = 0;
                do
                {
                    Sleep(1);
                    if (++attempts > 200)
                    {
                        if (g_chat)
                            g_chat->PrintDebug("Warning: Model %u wouldn't load in time!", static_cast<unsigned>(model));
                        return;
                    }
                }
                while (!g_game->IsModelAvailable(model));
            }
        }
    }

    Virtual<void(__thiscall*)(void*)>(m_pGameEntity, 0x20)(m_pGameEntity);
    Write(m_pGameEntity, 0x22, static_cast<std::uint16_t>(model));
    Virtual<void(__thiscall*)(void*, int)>(m_pGameEntity, 0x14)(m_pGameEntity, model);
}

int CEntity::GetModelIndex() const
{
    return IsValid() ? Read<std::uint16_t>(m_pGameEntity, 0x22) : 0;
}
void CEntity::Teleport(CVector position)
{
    ZoneScoped;
    if (!IsValid())
        return;
    const int model = GetModelIndex();
    if (model == 538 || model == 537 || model == 449)
        scm::ProcessCommand(&SET_TRAIN_COORDINATES, m_handle, static_cast<double>(position.x), static_cast<double>(position.y), static_cast<double>(position.z));
    else
        Virtual<void(__thiscall*)(void*, CVector, bool)>(m_pGameEntity, 0x38)(m_pGameEntity, position, false);
}
float CEntity::GetDistanceToPoint(CVector position) const
{
    CVector entityPosition;
    return ReadWorldPosition(this, entityPosition) ? DistanceBetween(entityPosition, position) : MISSING_ENTITY_DISTANCE;
}
float CEntity::GetDistanceToLocalPlayer() const
{
    CVector entityPosition;
    CVector localPosition;
    if (!LocalPosition(localPosition) || !ReadWorldPosition(this, entityPosition))
        return MISSING_ENTITY_DISTANCE;

    return DistanceBetween(entityPosition, localPosition);
}
float CEntity::GetDistanceToLocalPlayerNoHeight() const
{
    CVector entityPosition;
    CVector localPosition;
    if (!LocalPosition(localPosition) || !ReadWorldPosition(this, entityPosition))
        return MISSING_ENTITY_DISTANCE;

    return DistanceBetween(entityPosition, localPosition, true);
}
float CEntity::GetDistanceToCamera() const
{
    CVector entityPosition;
    return ReadWorldPosition(this, entityPosition) ? DistanceBetween(entityPosition, CCamera::GetPosition()) : MISSING_ENTITY_DISTANCE;
}

BOOL CEntity::DoesExist() const
{
    return IsValid() && Read<void*>(m_pGameEntity, 0xB4);
}
void CEntity::Add()
{
    ZoneScoped;
    if (!IsValid() || DoesExist())
        return;
    SetSpeed({});
    SetTurnSpeed({});
    reinterpret_cast<void(__cdecl*)(void*)>(0x563220)(m_pGameEntity);
    CMatrix matrix = CMatrix::Unity();
    GetMatrix(&matrix);
    Teleport(matrix.GetPosition());
}
void CEntity::Remove()
{
    if (DoesExist())
        reinterpret_cast<void(__cdecl*)(void*)>(0x563280)(m_pGameEntity);
}
BOOL CEntity::EnforceWorldBoundries(float maxX, float minX, float maxY, float minY)
{
    ZoneScoped;
    if (!IsValid())
        return FALSE;
    CMatrix matrix = CMatrix::Unity();
    GetMatrix(&matrix);
    auto& position = matrix.GetPosition();
    CVector speed;
    GetSpeed(&speed);
    float* axis = nullptr;
    float direction = 0.0f;
    if (position.x > maxX)
    {
        axis = &speed.x;
        direction = -0.2f;
    }
    else if (position.x < minX)
    {
        axis = &speed.x;
        direction = 0.2f;
    }
    else if (position.y > maxY)
    {
        axis = &speed.y;
        direction = -0.2f;
    }
    else if (position.y < minY)
    {
        axis = &speed.y;
        direction = 0.2f;
    }
    if (!axis)
        return FALSE;
    if (*axis == 0.0f)
    {
        *axis = direction;
        speed.z = 0.1f;
    }
    SetSpeed(speed);
    position.z += 0.04f;
    SetMatrix(matrix);
    return TRUE;
}
BOOL CEntity::HasExceededWorldBoundries(float maxX, float minX, float maxY, float minY) const
{
    if (!IsValid())
        return FALSE;
    CMatrix matrix = CMatrix::Unity();
    GetMatrix(&matrix);
    const auto& position = matrix.GetPosition();
    return position.x > maxX || position.x < minX || position.y > maxY || position.y < minY;
}
void CEntity::GetEulerInverted(float* x, float* y, float* z) const
{
    ZoneScoped;
    if (!x || !y || !z || !IsValid())
        return;

    if (auto* matrix = Read<void*>(m_pGameEntity, 0x14))
        reinterpret_cast<void(__thiscall*)(void*, float*, float*, float*, int)>(0x59A840)(matrix, x, y, z, 0x15);

    InvertEulerR5(x);
    InvertEulerR5(y);
    InvertEulerR5(z);
}
BOOL CEntity::IsIgnored() const
{
    return m_pGameEntity && m_pGameEntity == *reinterpret_cast<void**>(0xB7CD68);
}
BOOL CEntity::IsStationary() const
{
    CVector speed;
    GetSpeed(&speed);
    return DoesExist() && speed.x == 0.0f && speed.y == 0.0f && speed.z == 0.0f;
}
BOOL CEntity::GetCollisionFlag() const
{
    return !IsValid() || (Read<std::uint32_t>(m_pGameEntity, 0x1C) & 1);
}
void CEntity::SetCollisionFlag(BOOL enabled)
{
    if (IsValid())
    {
        auto flags = Read<std::uint32_t>(m_pGameEntity, 0x1C);
        Write(m_pGameEntity, 0x1C, enabled ? flags | 1 : flags & ~1u);
    }
}
void CEntity::SetCollisionProcessed(BOOL processed)
{
    ZoneScoped;
    if (IsValid())
    {
        auto flags = Read<std::uint32_t>(m_pGameEntity, 0x1C);
        Write(m_pGameEntity, 0x1C, processed ? flags & 0x7FFFFFFD : flags | 0x80000002);
    }
}
RwObject* CEntity::GetRwObject() const
{
    return IsValid() ? Read<RwObject*>(m_pGameEntity, 0x18) : nullptr;
}
void CEntity::DeleteRwObject()
{
    if (GetRwObject())
        Virtual<void(__thiscall*)(void*)>(m_pGameEntity, 0x20)(m_pGameEntity);
}
void CEntity::UpdateRwFrame()
{
    ZoneScoped;
    auto* object = GetRwObject();
    auto* matrix = IsValid() ? Read<void*>(m_pGameEntity, 0x14) : nullptr;
    if (!object || !object->parent || !matrix)
        return;
    auto* frame = static_cast<RwFrame*>(object->parent);
    reinterpret_cast<void(__thiscall*)(void*, RwMatrix*)>(0x59AD70)(matrix, &frame->modelling);
    reinterpret_cast<void(__thiscall*)(void*)>(0x532B00)(m_pGameEntity);
}
void CEntity::UpdateMatrixInWorld(const CMatrix& matrix)
{
    ZoneScoped;
    if (!IsValid() || !Read<void*>(m_pGameEntity, 0x14))
        return;
    const bool inWorld = DoesExist() != FALSE;
    if (inWorld)
        Virtual<void(__thiscall*)(void*)>(m_pGameEntity, 0x0C)(m_pGameEntity);
    SetMatrix(matrix);
    UpdateRwFrame();
    if (inWorld)
        Virtual<void(__thiscall*)(void*)>(m_pGameEntity, 8)(m_pGameEntity);
}
