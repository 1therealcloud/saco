#include <stdafx.h>
#include "CCamera.h"
#include "patch/hooks.h"
#include "main.h"
#include "game/CGame.h"

#include "Scripting.h"
#include "utils/math/CMatrix.h"

#include <cmath>

namespace
{
    constexpr std::uintptr_t GTA_CAMERA = 0x00B6F028;
    constexpr std::uintptr_t GTA_CAMERA_MATRIX = 0x00B6F99C;
    constexpr std::uintptr_t GTA_CAMERA_POSITION = 0x00B6F9CC;
    constexpr std::uintptr_t GTA_CAMERA_TAKE_CONTROL = 0x0050C7C0;

    constexpr std::uintptr_t GTA_ENTITY_MATRIX_OFFSET = 0x14;
    constexpr std::uintptr_t GTA_CAMERA_PROCESS_CALL = 0x0053C104;
    constexpr std::uintptr_t GTA_CAMERA_PROCESS = 0x0052B730;

    constexpr ::scm::OpcodeInfo CMD_FADE{0x016A, "ii"};
    constexpr ::scm::OpcodeInfo CMD_CAMERA_BEHIND_PLAYER{0x0373, ""};
    constexpr ::scm::OpcodeInfo CMD_POINT_CAMERA{0x0160, "fffi"};
    constexpr ::scm::OpcodeInfo CMD_RESTORE_CAMERA_JUMPCUT{0x02EB, ""};
    constexpr ::scm::OpcodeInfo CMD_SET_CAMERA{0x015F, "ffffff"};
    constexpr ::scm::OpcodeInfo CMD_RESTORE_USER_DEFINED{0x0925, ""};
    constexpr ::scm::OpcodeInfo CMD_MOVE_CAMERA{0x0936, "ffffffii"};
    constexpr ::scm::OpcodeInfo CMD_TRACK_CAMERA{0x0920, "ffffffii"};
    constexpr ::scm::OpcodeInfo CMD_LOCK_TARGET_POINT{0x092F, "i"};
    constexpr ::scm::OpcodeInfo CMD_LOCK_POSITION{0x0930, "i"};

    using GtaTakeControlFn = void(__thiscall*)(void* camera, void* target, short mode, short switchType, int whoIsInControl);

    bool IsValidCameraPosition(const CVector& position)
    {
        return position.x > -20000.0f && position.x < 20000.0f && position.y > -20000.0f && position.y < 20000.0f && position.z > -10000.0f && position.z < 100000.0f;
    }

    bool HasNativeMatrix(const CEntity* entity)
    {
        if (!entity || !entity->IsValid())
            return false;

        const auto* native = reinterpret_cast<const std::uint8_t*>(entity->GetNativeHandle());
        if (!native)
            return false;

        // gta sa 1.0: CPlaceable::m_pMatrix
        return *reinterpret_cast<void* const*>(native + GTA_ENTITY_MATRIX_OFFSET) != nullptr;
    }

}

bool CCamera::InstallProcessHook()
{
    // r5 redirects this call, not the whole gta camera function.
    if (!patch::expect_call(GTA_CAMERA_PROCESS_CALL, GTA_CAMERA_PROCESS))
        return false;

    return patch::call(GTA_CAMERA_PROCESS_CALL, reinterpret_cast<const void*>(&CameraProcessHook));
}

CVector CCamera::GetPosition()
{
    return *reinterpret_cast<const CVector*>(GTA_CAMERA_POSITION);
}

bool CCamera::HasLineOfSight(const CVector& position)
{
    // r5 traces from the label to the active camera's aim source, not the camera matrix
    const auto& source = *reinterpret_cast<const CVector*>(0xB6F338);
    using IsLineOfSightClear = bool(__cdecl*)(const CVector&, const CVector&, bool, bool, bool, bool, bool, bool, bool);
    return reinterpret_cast<IsLineOfSightClear>(0x56A490)(position, source, true, false, false, true, false, false, false);
}

CVector CCamera::TraceTarget(const CVector& origin, const CVector& target, bool vehicles, bool objects)
{
    alignas(4) std::uint8_t collision[44]{};
    void* entity = nullptr;
    using Trace = bool(__cdecl*)(const CVector&, const CVector&, void*, void**, bool, bool, bool, bool, bool, bool, bool, bool);
    if (reinterpret_cast<Trace>(0x56BA00)(origin, target, collision, &entity, true, vehicles, false, objects, false, false, false, false) && entity)
        return *reinterpret_cast<const CVector*>(collision);
    return target;
}

CCamera::CCamera()
{
    m_pAttachedTo = nullptr;
    m_pMatrix = reinterpret_cast<RwMatrixTag*>(GTA_CAMERA_MATRIX);
}

CCamera::~CCamera()
{
    m_pAttachedTo = nullptr;

    // r5 clears only the low byte of this pointer
    reinterpret_cast<std::uint8_t*>(&m_pMatrix)[0] = 0;
}

void CCamera::Fade(BOOL in)
{
    scm::ProcessCommand(&CMD_FADE, 500, in);
}

void CCamera::GetMatrix(CMatrix* pMatrix) const
{
    pMatrix->GetRight().x = m_pMatrix->right.x;
    pMatrix->GetRight().y = m_pMatrix->right.y;
    pMatrix->GetRight().z = m_pMatrix->right.z;

    pMatrix->GetForward().x = m_pMatrix->up.x;
    pMatrix->GetForward().y = m_pMatrix->up.y;
    pMatrix->GetForward().z = m_pMatrix->up.z;

    pMatrix->GetUp().x = m_pMatrix->at.x;
    pMatrix->GetUp().y = m_pMatrix->at.y;
    pMatrix->GetUp().z = m_pMatrix->at.z;

    pMatrix->GetPosition().x = m_pMatrix->pos.x;
    pMatrix->GetPosition().y = m_pMatrix->pos.y;
    pMatrix->GetPosition().z = m_pMatrix->pos.z;
}

void CCamera::SetMatrix(CMatrix matrix)
{
    m_pMatrix->right.x = matrix.GetRight().x;
    m_pMatrix->right.y = matrix.GetRight().y;
    m_pMatrix->right.z = matrix.GetRight().z;

    m_pMatrix->up.x = matrix.GetForward().x;
    m_pMatrix->up.y = matrix.GetForward().y;
    m_pMatrix->up.z = matrix.GetForward().z;

    m_pMatrix->at.x = matrix.GetUp().x;
    m_pMatrix->at.y = matrix.GetUp().y;
    m_pMatrix->at.z = matrix.GetUp().z;

    m_pMatrix->pos.x = matrix.GetPosition().x;
    m_pMatrix->pos.y = matrix.GetPosition().y;
    m_pMatrix->pos.z = matrix.GetPosition().z;
}

void CCamera::TakeControl(void* pTarget, short nModeToGoTo, short nTypeOfSwitch)
{
    const auto takeControl = reinterpret_cast<GtaTakeControlFn>(GTA_CAMERA_TAKE_CONTROL);
    takeControl(reinterpret_cast<void*>(GTA_CAMERA), pTarget, nModeToGoTo, nTypeOfSwitch, 1);
}

void CCamera::SetMoveVector(const CVector* from, const CVector* to, int time, bool smooth)
{
    SetMoveVector(from, to, time, static_cast<std::uint8_t>(smooth));
}

void CCamera::SetMoveVector(const CVector* from, const CVector* to, int time, std::uint8_t cutType)
{
    scm::ProcessCommand(&CMD_RESTORE_USER_DEFINED);
    scm::ProcessCommand(&CMD_LOCK_POSITION, 1);
    scm::ProcessCommand(&CMD_MOVE_CAMERA, from->x, from->y, from->z, to->x, to->y, to->z, time, static_cast<int>(cutType));
}

void CCamera::SetTrackVector(const CVector* from, const CVector* to, int time, bool smooth)
{
    SetTrackVector(from, to, time, static_cast<std::uint8_t>(smooth));
}

void CCamera::SetTrackVector(const CVector* from, const CVector* to, int time, std::uint8_t cutType)
{
    scm::ProcessCommand(&CMD_LOCK_TARGET_POINT, 1);
    scm::ProcessCommand(&CMD_TRACK_CAMERA, from->x, from->y, from->z, to->x, to->y, to->z, time, static_cast<int>(cutType));
}

void CCamera::Attach(CEntity* owner)
{
    m_pAttachedTo = owner;
    if (!HasNativeMatrix(owner))
        return;

    CMatrix matrix = CMatrix::Unity();
    owner->GetMatrix(&matrix);

    const CVector& position = matrix.GetPosition();
    if (IsValidCameraPosition(position))
        SetMoveVector(&position, &position, 100, true);
}

void CCamera::SetToOwner()
{
    if (!HasNativeMatrix(m_pAttachedTo))
        return;

    CMatrix matrix = CMatrix::Unity();
    m_pAttachedTo->GetMatrix(&matrix);

    const CVector& position = matrix.GetPosition();
    if (IsValidCameraPosition(position))
        SetMoveVector(&position, &position, 100, true);
}

float CCamera::GetDistanceToPoint(const CVector* pPoint) const
{
    const CVector cameraPosition = GetPosition();

    const float x = pPoint->x - cameraPosition.x;
    const float y = pPoint->y - cameraPosition.y;
    const float z = pPoint->z - cameraPosition.z;

    return std::sqrt(x * x + y * y + z * z);
}

void CCamera::Restore()
{
    scm::ProcessCommand(&CMD_LOCK_POSITION, 0);
    scm::ProcessCommand(&CMD_RESTORE_USER_DEFINED);

    m_pAttachedTo = nullptr;

    scm::ProcessCommand(&CMD_CAMERA_BEHIND_PLAYER);
    scm::ProcessCommand(&CMD_RESTORE_CAMERA_JUMPCUT);
}

void CCamera::Set(CVector position, CVector rotation)
{
    scm::ProcessCommand(&CMD_RESTORE_USER_DEFINED);

    m_pAttachedTo = nullptr;

    scm::ProcessCommand(&CMD_SET_CAMERA, position.x, position.y, position.z, rotation.x, rotation.y, rotation.z);
}

void CCamera::PointAt(CVector point, int nSwitchStyle)
{
    scm::ProcessCommand(&CMD_RESTORE_USER_DEFINED);

    m_pAttachedTo = nullptr;

    scm::ProcessCommand(&CMD_POINT_CAMERA, point.x, point.y, point.z, nSwitchStyle);
}

void CCamera::Detach()
{
    m_pAttachedTo = nullptr;

    scm::ProcessCommand(&CMD_RESTORE_CAMERA_JUMPCUT);
}
