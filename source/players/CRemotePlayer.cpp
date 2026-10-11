#include <stdafx.h>
#include "main.h"
#include "net/CNetGame.h"
#include "players/PlayerColors.h"

#include "CRemotePlayer.h"

#include "CLocalPlayer.h"
#include "game/CGame.h"
#include "game/BulletSync.h"
#include "game/SpecialAction.h"
#include "game/entities/CObject.h"
#include "game/entities/CPed.h"
#include "game/entities/CVehicle.h"
#include "gui/CChat.h"
#include "pools/CObjectPool.h"
#include "pools/CVehiclePool.h"
#include "utils/math/CQuaternion.h"
#include "sync/AimStuff.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_map>

namespace
{

    struct RuntimeState
    {
        D3DCOLOR color = 0xFFFFFFFF;
        CRemotePlayer::TICK entryExitDeadline = 0;
        bool enteringVehicle = false;
        bool exitingVehicle = false;
        D3DCOLOR markerColor = 0;
    };

    std::unordered_map<CRemotePlayer*, RuntimeState> g_runtime;

    constexpr float ONFOOT_EPSILON = 0.00001f;
    constexpr float ONFOOT_SNAP_XY = 2.0f;
    constexpr float ONFOOT_SNAP_Z = 1.0f;
    constexpr float ONFOOT_CORRECTION = 0.1f;
    constexpr float ONFOOT_ROTATION_SLERP = 0.75f;
    constexpr float INCAR_EPSILON = 0.05f;
    constexpr float INCAR_SNAP_XY = 8.0f;
    constexpr float INCAR_SNAP_Z = 0.5f;
    constexpr float INCAR_AIR_WATER_SNAP_Z = 2.0f;
    constexpr float INCAR_CORRECTION = 0.06f;
    constexpr float INCAR_MIN_SPEED = 0.01f;
    constexpr float INCAR_ROTATION_SLERP = 0.75f;
    constexpr float INCAR_MAX_TURN_SPEED = 0.02f;
    constexpr float UNOCCUPIED_DEAD_ZONE = 0.1f;
    constexpr float UNOCCUPIED_SNAP_XY = 6.0f;
    constexpr float UNOCCUPIED_SNAP_Z = 3.0f;
    constexpr float UNOCCUPIED_CORRECTION_DEAD_ZONE = 0.05f;
    constexpr float UNOCCUPIED_CORRECTION = 0.06f;
    constexpr float TRAILER_DEAD_ZONE = 0.5f;
    constexpr float TRAILER_SNAP_XY = 6.0f;
    constexpr float TRAILER_SNAP_Z = 3.0f;
    constexpr float TRAILER_CORRECTION_DEAD_ZONE = 0.05f;
    constexpr float TRAILER_CORRECTION = 0.025f;
    constexpr float DISTANCE_INVALID = 10000.0f;
    constexpr CRemotePlayer::TICK ENTRY_EXIT_ANIM_MS = 500;
    constexpr CRemotePlayer::TICK HEAD_UPDATE_MS = 1000;
    constexpr CRemotePlayer::TICK HEAD_LOOK_MS = 200;
    constexpr CRemotePlayer::TICK TIMEOUT_MS = 3000;
    constexpr CRemotePlayer::TICK IDLE_TIMEOUT_MS = 1500;

    AimStuff::Aim BuildRemoteAim(const Synchronization::AimData& data)
    {
        const auto& front = data.m_aimf1;
        const double angle = std::atan2(static_cast<double>(front.x), static_cast<double>(front.y)) - 1.5707963705062866;
        const double sine = std::sin(angle);
        const double cosine = std::cos(angle);

        AimStuff::Aim aim{};
        aim.front = front;
        aim.source = data.m_aimPos;
        aim.sourceBeforeLookBehind = data.m_aimPos;
        aim.up = CVector{static_cast<float>(-cosine * front.z), static_cast<float>(sine * front.z), static_cast<float>(cosine * front.x - sine * front.y)};
        return aim;
    }

    void ApplyOnfootWeapon(CPed* ped, std::uint8_t packedWeapon)
    {
        if (!ped || !ped->HasNativePed())
            return;

        const int weapon = packedWeapon & 0x3F;
        if (weapon > 46 || ped->GetCurrentWeapon() == weapon)
            return;

        ped->SetArmedWeapon(weapon);
        if (ped->GetCurrentWeapon() != weapon)
        {
            ped->GiveWeapon(weapon, 9999);
            if (ped->GetCurrentWeapon() != weapon)
                ped->SetArmedWeapon(weapon);
        }
    }

    void ApplyRemoteWeaponState(CPed* ped, std::uint8_t state)
    {
        auto* nativePed = ped ? static_cast<std::uint8_t*>(ped->GetNativePedHandle()) : nullptr;
        if (!nativePed || state > Synchronization::AimData::WS_RELOADING)
            return;

        const auto slot = nativePed[0x718];
        if (slot >= 13)
            return;
        auto* weapon = nativePed + 0x5A0 + static_cast<std::size_t>(slot) * 0x1C;
        std::uint32_t value{};
        if (state == Synchronization::AimData::WS_RELOADING)
        {
            value = 2;
            std::memcpy(weapon + 4, &value, sizeof(value));
        }
        else if (state == Synchronization::AimData::WS_MORE_BULLETS)
        {
            std::memcpy(&value, weapon + 8, sizeof(value));
            if (value < 2)
            {
                value = 2;
                std::memcpy(weapon + 8, &value, sizeof(value));
            }
        }
        else
        {
            value = state;
            std::memcpy(weapon + 8, &value, sizeof(value));
        }
    }

    RuntimeState& Runtime(CRemotePlayer* player)
    {
        return g_runtime[player];
    }

    bool Finite(float value)
    {
        return std::isfinite(value);
    }

    bool FiniteVector(const CVector& value)
    {
        return Finite(value.x) && Finite(value.y) && Finite(value.z);
    }

    bool FiniteQuaternion(const float (&value)[4])
    {
        return Finite(value[0]) && Finite(value[1]) && Finite(value[2]) && Finite(value[3]);
    }

    float Offset(float a, float b)
    {
        return std::fabs(a - b);
    }

    bool NeedsOnfootSnap(const CVector& a, const CVector& b)
    {
        return Offset(a.x, b.x) > ONFOOT_SNAP_XY || Offset(a.y, b.y) > ONFOOT_SNAP_XY || Offset(a.z, b.z) > ONFOOT_SNAP_Z;
    }

    bool PositionErrorExceeds(const CVector& target, const CVector& current, float xy, float z)
    {
        return Offset(target.x, current.x) > xy || Offset(target.y, current.y) > xy || Offset(target.z, current.z) > z;
    }

    void CorrectVelocity(CVector& speed, const CVector& target, const CVector& current, float deadZone, float factor)
    {
        if (Offset(target.x, current.x) > deadZone)
            speed.x += (target.x - current.x) * factor;
        if (Offset(target.y, current.y) > deadZone)
            speed.y += (target.y - current.y) * factor;
        if (Offset(target.z, current.z) > deadZone)
            speed.z += (target.z - current.z) * factor;
    }

    CVector PositionOf(const CEntity* entity)
    {
        CMatrix matrix = CMatrix::Unity();
        if (entity)
            entity->GetMatrix(&matrix);
        return matrix.GetPosition();
    }

    CVehicle* ResolveVehicle(CRemotePlayer::ID id)
    {
        auto* pools = g_netGame ? g_netGame->m_pPools : nullptr;
        auto* vehiclePool = pools ? pools->m_pVehicle : nullptr;

        if (id == CRemotePlayer::INVALID_ID || !vehiclePool)
            return nullptr;
        return vehiclePool->Get(id);
    }

    CObject* ResolveObject(CRemotePlayer::ID encodedId)
    {
        auto* pools = g_netGame ? g_netGame->m_pPools : nullptr;
        auto* objectPool = pools ? pools->m_pObject : nullptr;

        if (!objectPool || encodedId < CVehiclePool::MAX_VEHICLES)
            return nullptr;

        const auto id = static_cast<CObjectPool::ID>(encodedId - CVehiclePool::MAX_VEHICLES);
        if (id >= CObjectPool::MAX_OBJECTS)
            return nullptr;
        return objectPool->Get(id);
    }

    CPed* LocalPed()
    {
        auto* pools = g_netGame ? g_netGame->m_pPools : nullptr;
        auto* localPlayer = pools && pools->m_pPlayer ? pools->m_pPlayer->GetLocalPlayer() : nullptr;

        return localPlayer ? localPlayer->GetPed() : nullptr;
    }

    CQuaternion SyncQuaternion(const float (&value)[4])
    {
        // r5 sync stores w,x,y,z; our math stores x,y,z,w
        CQuaternion result{value[1], value[2], value[3], value[0]};
        if (result.GetLengthSquared() > 0.000001f)
            result.Normalise();
        else
            result = CQuaternion{0.0f, 0.0f, 0.0f, 1.0f};
        return result;
    }

    CQuaternion MatrixQuaternion(const CMatrix& matrix)
    {
        CQuaternion result{};
        result.Set(matrix.ToRwMatrix());
        if (result.GetLengthSquared() > 0.000001f)
            result.Normalise();
        return result;
    }

    void DestroyPed(CPed*& ped)
    {
        if (!ped)
            return;
        ped->Destroy();
        delete ped;
        ped = nullptr;
    }

    bool IsTrainModel(int model)
    {
        return model == 449 || model == 537 || model == 538;
    }

    void ProcessRemoteMarker(CRemotePlayer& player)
    {
        auto* game = g_game.get();
        auto* net = g_netGame.get();
        if (!game)
            return;

        auto& runtime = Runtime(&player);
        const int mode = net && net->m_pSettings && net->m_nGameState == CNetGame::GAME_MODE_CONNECTED ? net->m_pSettings->m_nPlayerMarkersMode : MARKERS_MODE_OFF;
        const bool streamed = player.m_pPed && player.m_pPed->DoesExist() && player.m_nState != CRemotePlayer::PLAYER_STATE_WASTED;
        const D3DCOLOR color = player.GetColorAsRGBA();
        const bool show = player.m_nState != CRemotePlayer::PLAYER_STATE_WASTED && (color & 0xFFu) != 0 &&
                          (mode == MARKERS_MODE_GLOBAL ? (streamed || player.m_bMarkerState) : (mode == MARKERS_MODE_STREAMED && streamed));

        if (!show)
        {
            if (player.m_marker)
                game->DeleteMarker(player.m_marker);
            player.m_marker = 0;
            runtime.markerColor = 0;
            return;
        }

        CVector position =
            streamed ? PositionOf(player.m_pPed)
                     : CVector{static_cast<float>(player.m_markerPosition.x), static_cast<float>(player.m_markerPosition.y), static_cast<float>(player.m_markerPosition.z)};
        if (streamed && player.m_nState == CRemotePlayer::PLAYER_STATE_DRIVER && player.m_pVehicle && player.m_pVehicle->DoesExist())
            position = PositionOf(player.m_pVehicle);
        if (!FiniteVector(position))
            return;

        if (player.m_marker && runtime.markerColor != color)
        {
            game->DeleteMarker(player.m_marker);
            player.m_marker = 0;
        }

        if (!player.m_marker)
        {
            player.m_marker = game->CreateMarker(0, position, static_cast<int>(color), 1);
            runtime.markerColor = color;
        }
        else if (!game->SetMarkerPosition(player.m_marker, position))
        {
            player.m_marker = 0;
            runtime.markerColor = 0;
        }
    }
}

CRemotePlayer::CRemotePlayer()
{
    field_1 = 0;
    m_bDrawLabels = TRUE;
    m_bHasJetpack = FALSE;
    m_nSpecialAction = 0;
    std::memset(pad_2, 0, sizeof(pad_2));
    m_incarData = {};
    m_trailerData = {};
    m_aimData = {};
    m_passengerData = {};
    m_onfootData = {};
    m_nTeam = 0xFF;
    m_nState = PLAYER_STATE_NONE;
    m_nSeatId = 0;
    field_3 = 0;
    m_bPassengerDriveBy = FALSE;
    std::memset(pad_1, 0, sizeof(pad_1));
    m_positionDifference = CVector{};
    m_incarTargetRotation = {};
    m_onfootTargetPosition = CVector{};
    m_onfootTargetSpeed = CVector{};
    m_incarTargetPosition = CVector{};
    m_incarTargetSpeed = CVector{};
    m_fReportedArmour = 0.0f;
    m_fReportedHealth = 0.0f;
    m_animation = {};
    m_nUpdateType = UPDATE_TYPE_NONE;
    m_lastTimestamp = 0;
    m_bPerformingCustomAnimation = FALSE;
    m_nStatus = 0;
    m_head = {};
    m_pPed = nullptr;
    m_pVehicle = nullptr;
    m_nId = INVALID_ID;
    m_nVehicleId = INVALID_ID;
    m_bMarkerState = FALSE;
    m_markerPosition = {};
    m_marker = 0;

    const TICK now = GetTickCount();
    m_lastUpdate = now;
    m_head.m_lastUpdate = now;
    m_head.m_lastLook = now;
    m_incarTargetRotation.real = 1.0f;

    Runtime(this) = {};
    ResetData();
}

CRemotePlayer::~CRemotePlayer()
{
    Remove();
    g_runtime.erase(this);
}

void CRemotePlayer::ProcessHead()
{
    if (!m_pPed || !m_pPed->HasNativePed() || m_pPed->IsDead() || m_pPed->IsHoldingStuff())
        return;

    const CVector position = PositionOf(m_pPed);
    if (!FiniteVector(position))
        return;

    const TICK now = GetTickCount();
    if (now - m_head.m_lastUpdate > HEAD_UPDATE_MS)
    {
        if (!FiniteVector(m_aimData.m_aimf1))
            return;
        m_head.m_direction = m_aimData.m_aimf1 * 20.0f;
        m_head.m_lastUpdate = now;
    }

    if (now - m_head.m_lastLook > HEAD_LOOK_MS)
    {
        const CVector target = position + m_head.m_direction;
        if (FiniteVector(target))
            m_pPed->LookAt(target);
        m_head.m_lastLook = now;
    }
}

void CRemotePlayer::SetMarkerState(BOOL state)
{
    m_bMarkerState = state ? TRUE : FALSE;
}

void CRemotePlayer::SetMarkerPosition(int x, int y, int z)
{
    m_markerPosition = {x, y, z};
}

BOOL CRemotePlayer::SurfingOnVehicle()
{
    if (m_nState != PLAYER_STATE_ONFOOT)
        return FALSE;

    const ID id = m_onfootData.m_nSurfingVehicleId;
    return id != 0 && id != INVALID_ID && id < 3000;
}

BOOL CRemotePlayer::SurfingOnObject()
{
    if (m_nState != PLAYER_STATE_ONFOOT)
        return FALSE;

    const ID id = m_onfootData.m_nSurfingVehicleId;
    return id >= CVehiclePool::MAX_VEHICLES && id < 3000;
}

void CRemotePlayer::ProcessSurfing()
{
    if (!m_pPed || !m_pPed->HasNativePed() || m_pPed->IsInVehicle())
        return;

    const CVector offset = m_onfootData.m_surfingOffset;
    if (!FiniteVector(offset) || std::fabs(offset.x) >= 100.0f || std::fabs(offset.y) >= 100.0f || std::fabs(offset.z) >= 100.0f)
        return;

    CEntity* entity = nullptr;
    if (SurfingOnObject())
        entity = ResolveObject(m_onfootData.m_nSurfingVehicleId);
    else if (SurfingOnVehicle())
        entity = ResolveVehicle(m_onfootData.m_nSurfingVehicleId);

    if (!entity || !entity->IsValid())
        return;

    CMatrix entityMatrix = CMatrix::Unity();
    entity->GetMatrix(&entityMatrix);
    const CVector target = entityMatrix.TransformPoint(offset);
    if (!FiniteVector(target))
        return;

    CMatrix pedMatrix = CMatrix::Unity();
    m_pPed->GetMatrix(&pedMatrix);
    m_positionDifference = target - pedMatrix.GetPosition();
    pedMatrix.GetPosition() = target;
    m_pPed->SetMatrix(pedMatrix);

    CVector speed{};
    entity->GetSpeed(&speed);
    if (FiniteVector(speed))
        m_pPed->SetSpeed(speed);
}

void CRemotePlayer::OnEnterVehicle()
{
    if (!m_pPed || !m_pVehicle || !m_pVehicle->DoesExist())
        return;

    const int seat = m_nState == PLAYER_STATE_DRIVER ? 0 : static_cast<int>(m_nSeatId);
    if (seat < 0 || seat > 7)
        return;
    if (m_pPed->GetVehicle() == m_pVehicle->GetNativeVehicleHandle() && m_pPed->GetVehicleSeatIndex() == seat)
        return;

    m_pPed->PutIntoVehicle(m_pVehicle->m_handle, seat);
}

void CRemotePlayer::OnExitVehicle()
{
    if (!m_pPed || !m_pPed->GetVehicle())
        return;

    CMatrix matrix = CMatrix::Unity();
    m_pPed->GetMatrix(&matrix);
    m_pPed->WarpFromVehicle(matrix.GetPosition());
}

void CRemotePlayer::ProcessSpecialAction()
{
    if (!m_pPed || m_nState != PLAYER_STATE_ONFOOT)
        return;

    switch (m_nSpecialAction)
    {
        case SPECIAL_ACTION_NONE:
            if (m_pPed->IsHoldingStuff())
                m_pPed->DropStuff();
            if (m_pPed->DoesDancing())
                m_pPed->StopDancing();
            if (m_pPed->UsingCellphone())
                m_pPed->EnableCellphone(FALSE);
            if (m_pPed->DoesUrinating())
                m_pPed->StopUrinating();
            m_bHasJetpack = FALSE;
            break;

        case SPECIAL_ACTION_USEJETPACK:
            m_bHasJetpack = TRUE;
            break;

        case SPECIAL_ACTION_DANCE1:
        case SPECIAL_ACTION_DANCE2:
        case SPECIAL_ACTION_DANCE3:
        case SPECIAL_ACTION_DANCE4:
            if (!m_pPed->DoesDancing() && !m_pPed->GetVehicle())
                m_pPed->StartDancing(m_nSpecialAction - SPECIAL_ACTION_DANCE1);
            if (m_pPed->DoesDancing())
                m_pPed->ProcessDancing();
            break;

        case SPECIAL_ACTION_HANDSUP:
            if (!m_pPed->DoesHandsUp() && !m_pPed->GetVehicle())
                m_pPed->HandsUp();
            break;

        case SPECIAL_ACTION_USECELLPHONE:
            if (!m_pPed->UsingCellphone() && !m_pPed->GetVehicle())
                m_pPed->EnableCellphone(TRUE);
            break;

        case SPECIAL_ACTION_STOPUSECELLPHONE:
            if (m_pPed->UsingCellphone())
                m_pPed->EnableCellphone(FALSE);
            break;

        case SPECIAL_ACTION_DRINK_BEER:
        case SPECIAL_ACTION_DRINK_WINE:
        case SPECIAL_ACTION_DRINK_SPRUNK:
        case SPECIAL_ACTION_SMOKE_CIGGY:
        {
            const auto action = m_nSpecialAction;
            const int type = action == SPECIAL_ACTION_DRINK_BEER ? 1 : action == SPECIAL_ACTION_DRINK_WINE ? 2 : action == SPECIAL_ACTION_DRINK_SPRUNK ? 3 : 4;
            if (m_pPed->GetStuff() != type)
                m_pPed->GiveStuff(type);
            break;
        }
        case SPECIAL_ACTION_URINATING:
            if (!m_pPed->DoesUrinating() && !m_pPed->GetVehicle())
                m_pPed->StartUrinating();
            break;

        default:
            break;
    }
}

void CRemotePlayer::UpdateOnfootSpeedAndPosition()
{
    if (!m_pPed || !FiniteVector(m_onfootTargetPosition) || !FiniteVector(m_onfootTargetSpeed))
        return;

    CMatrix matrix = CMatrix::Unity();
    m_pPed->GetMatrix(&matrix);
    const CVector current = matrix.GetPosition();

    m_positionDifference.x = Offset(m_onfootTargetPosition.x, current.x);
    m_positionDifference.y = Offset(m_onfootTargetPosition.y, current.y);
    m_positionDifference.z = Offset(m_onfootTargetPosition.z, current.z);

    if (m_positionDifference.x <= ONFOOT_EPSILON && m_positionDifference.y <= ONFOOT_EPSILON && m_positionDifference.z <= ONFOOT_EPSILON)
        return;

    if (!m_pPed->DoesExist() || NeedsOnfootSnap(m_onfootTargetPosition, current))
    {
        matrix.GetPosition() = m_onfootTargetPosition;
        m_pPed->SetMatrix(matrix);
        return;
    }

    CVector speed{};
    m_pPed->GetSpeed(&speed);

    if (m_positionDifference.x > ONFOOT_EPSILON)
        speed.x += (m_onfootTargetPosition.x - current.x) * ONFOOT_CORRECTION;
    if (m_positionDifference.y > ONFOOT_EPSILON)
        speed.y += (m_onfootTargetPosition.y - current.y) * ONFOOT_CORRECTION;
    if (m_positionDifference.z > ONFOOT_EPSILON)
        speed.z += (m_onfootTargetPosition.z - current.z) * ONFOOT_CORRECTION;

    if (FiniteVector(speed))
        m_pPed->SetSpeed(speed);
}

void CRemotePlayer::UpdateOnfootRotation()
{
    if (!m_pPed || !FiniteQuaternion(m_onfootData.m_fQuaternion))
        return;

    CMatrix matrix = CMatrix::Unity();
    m_pPed->GetMatrix(&matrix);
    const CVector position = matrix.GetPosition();

    const CQuaternion current = MatrixQuaternion(matrix);
    const CQuaternion target = SyncQuaternion(m_onfootData.m_fQuaternion);
    CQuaternion rotation{};

    // SA-MP R5 passes the packet quaternion as D3DXQuaternionSlerp's first
    // input and the current ped quaternion as the second one with t = 0.75.
    // This is equivalent to moving from current toward target by 25% of the
    // remaining angular distance each frame, not 75%.
    rotation.Slerp(target, current, ONFOOT_ROTATION_SLERP);

    matrix.SetRotate(rotation);
    matrix.GetPosition() = position;
    m_pPed->SetMatrix(matrix);

    const CVector direction = matrix.GetForward();
    float heading = std::atan2(-direction.x, direction.y) * 57.295776f;
    if (heading < 0.0f)
        heading += 360.0f;
    else if (heading >= 360.0f)
        heading -= 360.0f;
    m_pPed->ForceRotation(heading);
}

void CRemotePlayer::SetOnfootTargetSpeedAndPosition(CVector* position, CVector* speed)
{
    if (!position || !speed || !FiniteVector(*position) || !FiniteVector(*speed))
        return;

    m_onfootTargetPosition = *position;
    m_onfootTargetSpeed = *speed;
    if (m_pPed)
        m_pPed->SetSpeed(m_onfootTargetSpeed);
}

void CRemotePlayer::UpdateIncarSpeedAndPosition()
{
    if (!m_pVehicle || !m_pVehicle->DoesExist() || !FiniteVector(m_incarTargetPosition) || !FiniteVector(m_incarTargetSpeed))
        return;

    CMatrix matrix = CMatrix::Unity();
    m_pVehicle->GetMatrix(&matrix);
    if (!m_pVehicle->CEntity::DoesExist())
    {
        matrix.GetPosition() = m_incarTargetPosition;
        m_pVehicle->SetMatrix(matrix);
        m_pVehicle->SetSpeed(m_incarTargetSpeed);
        return;
    }

    const CVector current = matrix.GetPosition();
    m_positionDifference.x = Offset(m_incarTargetPosition.x, current.x);
    m_positionDifference.y = Offset(m_incarTargetPosition.y, current.y);
    m_positionDifference.z = Offset(m_incarTargetPosition.z, current.z);

    if (m_positionDifference.x <= INCAR_EPSILON && m_positionDifference.y <= INCAR_EPSILON && m_positionDifference.z <= INCAR_EPSILON)
        return;

    const int subtype = m_pVehicle->GetSubtype();
    const float snapZ = subtype == 3 || subtype == 4 || subtype == 5 ? INCAR_AIR_WATER_SNAP_Z : INCAR_SNAP_Z;
    if (m_positionDifference.x > INCAR_SNAP_XY || m_positionDifference.y > INCAR_SNAP_XY || m_positionDifference.z > snapZ)
    {
        matrix.GetPosition() = m_incarTargetPosition;
        m_pVehicle->SetMatrix(matrix);
        m_pVehicle->SetSpeed(m_incarTargetSpeed);
        return;
    }

    CVector speed{};
    m_pVehicle->GetSpeed(&speed);
    if (m_positionDifference.x > INCAR_EPSILON)
        speed.x += (m_incarTargetPosition.x - current.x) * INCAR_CORRECTION;
    if (m_positionDifference.y > INCAR_EPSILON)
        speed.y += (m_incarTargetPosition.y - current.y) * INCAR_CORRECTION;
    if (m_positionDifference.z > INCAR_EPSILON)
        speed.z += (m_incarTargetPosition.z - current.z) * INCAR_CORRECTION;

    if (FiniteVector(speed) && (Offset(speed.x, 0.0f) > INCAR_MIN_SPEED || Offset(speed.y, 0.0f) > INCAR_MIN_SPEED || Offset(speed.z, 0.0f) > INCAR_MIN_SPEED))
        m_pVehicle->SetSpeed(speed);
}

void CRemotePlayer::UpdateIncarRotation()
{
    if (!m_pVehicle || !m_pVehicle->DoesExist())
        return;

    CQuaternion target{m_incarTargetRotation.imag.x, m_incarTargetRotation.imag.y, m_incarTargetRotation.imag.z, m_incarTargetRotation.real};
    if (!Finite(target.x) || !Finite(target.y) || !Finite(target.z) || !Finite(target.w) || target.GetLengthSquared() <= 0.000001f)
        return;
    target.Normalise();

    // r5's default turn-speed clamp
    CVector turnSpeed{};
    m_pVehicle->GetTurnSpeed(&turnSpeed);
    if (FiniteVector(turnSpeed))
    {
        turnSpeed.x = std::clamp(turnSpeed.x, -INCAR_MAX_TURN_SPEED, INCAR_MAX_TURN_SPEED);
        turnSpeed.y = std::clamp(turnSpeed.y, -INCAR_MAX_TURN_SPEED, INCAR_MAX_TURN_SPEED);
        turnSpeed.z = std::clamp(turnSpeed.z, -INCAR_MAX_TURN_SPEED, INCAR_MAX_TURN_SPEED);
        m_pVehicle->SetTurnSpeed(turnSpeed);
    }

    CMatrix matrix = CMatrix::Unity();
    m_pVehicle->GetMatrix(&matrix);
    const CQuaternion current = MatrixQuaternion(matrix);
    CQuaternion result{};
    result.Slerp(target, current, INCAR_ROTATION_SLERP);

    const CVector position = matrix.GetPosition();
    matrix.SetRotate(result);
    matrix.GetPosition() = position;
    m_pVehicle->SetMatrix(matrix);
}

void CRemotePlayer::SetIncarTargetSpeedAndPosition(CMatrix* matrix, CVector* position, CVector* speed)
{
    if (!matrix || !position || !speed || !FiniteVector(*position) || !FiniteVector(*speed))
        return;

    m_incarTargetPosition = *position;
    m_incarTargetSpeed = *speed;

    const CQuaternion rotation = MatrixQuaternion(*matrix);
    m_incarTargetRotation.real = rotation.real;
    m_incarTargetRotation.imag = rotation.imag;

    if (m_pVehicle && m_pVehicle->DoesExist())
        m_pVehicle->SetSpeed(*speed);
}

void CRemotePlayer::UpdateTrain(CMatrix* matrix, CVector* speed, float trainSpeed)
{
    if (!m_pVehicle || !m_pVehicle->DoesExist() || !matrix || !speed || !FiniteVector(*speed) || !Finite(trainSpeed))
        return;

    CMatrix current = CMatrix::Unity();
    m_pVehicle->GetMatrix(&current);
    const CVector target = matrix->GetPosition();

    if (Offset(target.x, current.GetPosition().x) > 10.0f || Offset(target.y, current.GetPosition().y) > 10.0f)
    {
        current.GetPosition() = target;
        m_pVehicle->SetMatrix(current);
    }

    // keep gta's train velocity
    m_pVehicle->SetTrainSpeed(trainSpeed);
}

void CRemotePlayer::Update(Synchronization::AimData* data)
{
    if (!data || !FiniteVector(data->m_aimf1) || !FiniteVector(data->m_aimPos) || !Finite(data->m_fAimZ))
        return;

    m_aimData = *data;
    if (!m_pPed || !m_pPed->HasNativePed())
        return;

    const auto slot = m_pPed->m_nPlayerNumber;
    if (slot < 2 || slot >= AimStuff::PLAYER_COUNT)
        return;

    const auto aim = BuildRemoteAim(*data);
    m_pPed->SetAim(&aim);
    m_pPed->SetAimZ(data->m_fAimZ);
    m_pPed->SetCameraMode(static_cast<char>(data->m_nCameraMode));
    m_pPed->SetCameraExtZoomAndAspectRatio(Synchronization::DecompressCameraExtZoom(data->m_nCameraExtZoom), Synchronization::DecompressAspectRatio(data->m_nAspectRatio));
    ApplyRemoteWeaponState(m_pPed, data->m_nWeaponState);
}

void CRemotePlayer::Update(Synchronization::UnoccupiedData* data)
{
    if (!data || data->m_nVehicleId == 0 || data->m_nVehicleId >= CVehiclePool::MAX_VEHICLES || !FiniteVector(data->m_roll) || !FiniteVector(data->m_direction) ||
        !FiniteVector(data->m_position) || !FiniteVector(data->m_speed) || !FiniteVector(data->m_turnSpeed) ||
        (m_nState == PLAYER_STATE_DRIVER && m_nVehicleId == data->m_nVehicleId))
        return;

    auto* pool = g_netGame ? g_netGame->GetVehiclePool() : nullptr;
    CVehicle* vehicle = pool ? pool->Get(data->m_nVehicleId) : nullptr;
    if (!vehicle || !vehicle->DoesExist() || vehicle->HasDriver() || vehicle->HasPlayerDriver() || vehicle->GetTractor())
        return;

    CMatrix matrix = CMatrix::Unity();
    vehicle->GetMatrix(&matrix);
    const CVector current = matrix.GetPosition();
    if (!FiniteVector(current))
        return;

    if (PositionErrorExceeds(data->m_position, current, UNOCCUPIED_DEAD_ZONE, UNOCCUPIED_DEAD_ZONE))
    {
        CVector right = data->m_roll;
        CVector forward = data->m_direction;
        if (right.SquaredMagnitude() < 0.000001f || forward.SquaredMagnitude() < 0.000001f)
            return;
        right.Normalise();
        forward.Normalise();
        CVector up = right.Cross(forward);
        if (up.SquaredMagnitude() < 0.000001f)
            return;
        up.Normalise();
        forward = up.Cross(right);
        forward.Normalise();

        matrix.GetRight() = right;
        matrix.GetForward() = forward;
        matrix.GetUp() = up;

        const bool snap = PositionErrorExceeds(data->m_position, current, UNOCCUPIED_SNAP_XY, UNOCCUPIED_SNAP_Z);
        if (snap)
            matrix.GetPosition() = data->m_position;
        vehicle->SetMatrix(matrix);

        CVector speed = data->m_speed;
        if (!snap)
            CorrectVelocity(speed, data->m_position, current, UNOCCUPIED_CORRECTION_DEAD_ZONE, UNOCCUPIED_CORRECTION);
        vehicle->SetSpeed(speed);
        vehicle->SetTurnSpeed(data->m_turnSpeed);
    }

    vehicle->m_bUnoccupiedSync = FALSE;
    vehicle->m_bRemoteUnocSync = TRUE;
    pool->m_nLastUndrivenId[data->m_nVehicleId] = m_nId;
    pool->m_lastUndrivenProcessTick[data->m_nVehicleId] = GetTickCount();
}

void CRemotePlayer::Update(Synchronization::TrailerData* data)
{
    if (!data || data->m_nId == 0 || data->m_nId >= CVehiclePool::MAX_VEHICLES || m_nState != PLAYER_STATE_DRIVER || m_nVehicleId == INVALID_ID ||
        !FiniteVector(data->m_position) || !FiniteQuaternion(data->m_fQuaternion) || !FiniteVector(data->m_speed) || !FiniteVector(data->m_turnSpeed))
        return;

    CVehicle* tractor = ResolveVehicle(m_nVehicleId);
    CVehicle* trailer = ResolveVehicle(data->m_nId);
    if (!tractor || !trailer || tractor == trailer || !tractor->DoesExist() || !trailer->DoesExist() || (!trailer->IsTrailer() && !tractor->IsTowtruck()))
        return;

    // reject late packets from an old trailer
    if (m_incarData.m_nTrailerId != data->m_nId)
        return;

    if (tractor->GetTrailer() != trailer)
    {
        if (CVehicle* attached = tractor->GetTrailer())
        {
            tractor->SetTrailer(attached);
            tractor->DetachTrailer();
            tractor->SetTrailer(nullptr);
        }
        tractor->SetTrailer(trailer);
        tractor->AttachTrailer();
    }

    m_trailerData = *data;
    CMatrix matrix = CMatrix::Unity();
    trailer->GetMatrix(&matrix);
    const CVector current = matrix.GetPosition();
    if (!FiniteVector(current))
        return;

    if (!PositionErrorExceeds(data->m_position, current, TRAILER_DEAD_ZONE, TRAILER_DEAD_ZONE))
        return;

    const CVector position = matrix.GetPosition();
    matrix.SetRotate(SyncQuaternion(data->m_fQuaternion));
    matrix.GetPosition() = position;

    const bool snap = PositionErrorExceeds(data->m_position, current, TRAILER_SNAP_XY, TRAILER_SNAP_Z);
    if (snap)
        matrix.GetPosition() = data->m_position;
    trailer->SetMatrix(matrix);

    if (snap)
        trailer->SetSpeed(data->m_speed);
    else
    {
        CVector speed{};
        trailer->GetSpeed(&speed);
        CorrectVelocity(speed, data->m_position, current, TRAILER_CORRECTION_DEAD_ZONE, TRAILER_CORRECTION);
        trailer->SetSpeed(speed);
    }
    trailer->SetTurnSpeed(data->m_turnSpeed);
}

void CRemotePlayer::ResetData()
{
    std::memset(&m_incarData, 0, sizeof(m_incarData));
    std::memset(&m_trailerData, 0, sizeof(m_trailerData));
    std::memset(&m_aimData, 0, sizeof(m_aimData));
    std::memset(&m_passengerData, 0, sizeof(m_passengerData));
    std::memset(&m_onfootData, 0, sizeof(m_onfootData));

    m_incarData.m_nVehicle = INVALID_ID;
    m_incarData.m_nTrailerId = INVALID_ID;
    m_trailerData.m_nId = INVALID_ID;
    m_passengerData.m_nVehicleId = INVALID_ID;
    m_onfootData.m_nSurfingVehicleId = INVALID_ID;

    m_nVehicleId = INVALID_ID;
    m_pVehicle = nullptr;
    m_nSeatId = 0;
    m_bPassengerDriveBy = FALSE;
    m_nSpecialAction = SPECIAL_ACTION_NONE;
    m_bHasJetpack = FALSE;
    m_positionDifference = CVector{};
    m_onfootTargetPosition = CVector{};
    m_onfootTargetSpeed = CVector{};
    m_incarTargetPosition = CVector{};
    m_incarTargetSpeed = CVector{};
    m_incarTargetRotation.real = 1.0f;
    m_incarTargetRotation.imag = CVector{};
    m_fReportedArmour = 0.0f;
    m_fReportedHealth = 0.0f;
    m_animation = {};
    m_nUpdateType = UPDATE_TYPE_NONE;
    m_lastTimestamp = 0;
    m_bPerformingCustomAnimation = FALSE;
    m_nStatus = PLAYER_STATUS_TIMEOUT;
    auto& runtime = Runtime(this);
    runtime.entryExitDeadline = 0;
    runtime.enteringVehicle = false;
    runtime.exitingVehicle = false;
}

float CRemotePlayer::GetDistanceToPlayer(CRemotePlayer* player)
{
    if (!m_pPed || !player || !player->m_pPed)
        return DISTANCE_INVALID;

    const CVector difference = PositionOf(m_pPed) - PositionOf(player->m_pPed);
    return difference.Magnitude();
}

float CRemotePlayer::GetDistanceToLocalPlayer()
{
    CPed* local = LocalPed();
    if (!m_pPed || !local)
        return DISTANCE_INVALID;

    const CVector difference = PositionOf(m_pPed) - PositionOf(local);
    return difference.Magnitude();
}

void CRemotePlayer::SetColor(D3DCOLOR color)
{
    Runtime(this).color = color;
    if (m_nId < PlayerColors::COUNT)
        PlayerColors::SetColor(m_nId, color);
}

D3DCOLOR CRemotePlayer::GetColorAsRGBA()
{
    return m_nId < PlayerColors::COUNT ? PlayerColors::GetColor(m_nId) : Runtime(this).color;
}

D3DCOLOR CRemotePlayer::GetColorAsARGB()
{
    const D3DCOLOR rgba = GetColorAsRGBA();
    return (rgba >> 8) | (rgba << 24);
}

void CRemotePlayer::EnterVehicle(ID id, BOOL passenger)
{
    CVehicle* vehicle = ResolveVehicle(id);
    if (!m_pPed || !vehicle || !vehicle->DoesExist())
        return;

    m_nVehicleId = id;
    m_pVehicle = vehicle;
    m_nSeatId = passenger ? 1 : 0;

    m_pPed->EnterVehicle(vehicle->m_handle, passenger);
    auto& runtime = Runtime(this);
    runtime.entryExitDeadline = GetTickCount() + ENTRY_EXIT_ANIM_MS;
    runtime.enteringVehicle = true;
    runtime.exitingVehicle = false;
}

void CRemotePlayer::ExitVehicle()
{
    if (!m_pPed)
        return;

    m_pPed->ExitVehicle();
    auto& runtime = Runtime(this);
    runtime.entryExitDeadline = GetTickCount() + ENTRY_EXIT_ANIM_MS;
    runtime.enteringVehicle = false;
    runtime.exitingVehicle = true;
}

void CRemotePlayer::ChangeState(char oldState, char newState)
{
    if (oldState == newState)
        return;

    if (oldState == PLAYER_STATE_DRIVER && newState == PLAYER_STATE_ONFOOT && m_nVehicleId != INVALID_ID)
    {
        CPed* localPed = LocalPed();
        auto* vehiclePool = g_netGame ? g_netGame->GetVehiclePool() : nullptr;
        if (localPed && localPed->HasNativePed() && localPed->IsInVehicle() && !localPed->IsPassenger() && vehiclePool)
        {
            void* nativeVehicle = localPed->GetVehicle();
            if (nativeVehicle && vehiclePool->Find(nativeVehicle) == m_nVehicleId)
            {
                CMatrix matrix = CMatrix::Unity();
                localPed->GetMatrix(&matrix);
                CVector position = matrix.GetPosition();
                if (FiniteVector(position))
                {
                    position.z += 1.0f;
                    localPed->WarpFromVehicle(position);
                    if (auto* game = g_game.get())
                        game->DisplayGameText("~r~Car Jacked~w~!", 1000, 5);
                }
            }
        }
    }

    m_nState = static_cast<unsigned char>(newState);
}

int CRemotePlayer::GetStatus()
{
    return m_nStatus;
}

void CRemotePlayer::Update(Synchronization::BulletData* data)
{
    if (!data || !m_pPed || !m_pPed->HasNativePed() || !g_netGame || data->m_nWeapon < 22 || data->m_nWeapon > 34 || data->m_nTargetType > 4)
        return;
    auto finite = [](const CVector& value) {
        return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
    };
    if (!finite(data->m_origin) || !finite(data->m_target) || !finite(data->m_center))
        return;
    if (data->m_nTargetType && (std::abs(data->m_center.x) > 300.0f || std::abs(data->m_center.y) > 300.0f || std::abs(data->m_center.z) > 300.0f))
        return;
    game::bullets::Shot shot{data->m_origin, data->m_target, data->m_center};
    if (data->m_nTargetType == 1)
    {
        if (data->m_nTargetId == m_nId)
            return;
        auto* players = g_netGame->GetPlayerPool();
        if (players && data->m_nTargetId == players->m_localInfo.m_nId)
            shot.entity = CPed::GetLocalPedHandle();
        else if (auto* player = players ? players->GetPlayer(data->m_nTargetId) : nullptr; player && player->m_pPed)
            shot.entity = player->m_pPed->GetNativePedHandle();
    }
    else if (data->m_nTargetType == 2)
    {
        auto* vehicles = g_netGame->GetVehiclePool();
        auto* vehicle = vehicles ? vehicles->Get(data->m_nTargetId) : nullptr;
        if (vehicle && vehicle->HasNativeVehicle())
            shot.entity = vehicle->GetNativeVehicleHandle();
    }
    if (m_pPed->GetCurrentWeapon() != data->m_nWeapon)
    {
        m_pPed->GiveWeapon(data->m_nWeapon, 9999);
        m_pPed->SetArmedWeapon(data->m_nWeapon);
    }
    if (m_pPed->GetCurrentWeapon() == data->m_nWeapon)
        game::bullets::Fire(*m_pPed, shot, CNetGame::RefLagCompMode());
}

void CRemotePlayer::Process()
{
    m_pVehicle = m_nVehicleId == INVALID_ID ? nullptr : ResolveVehicle(m_nVehicleId);
    ProcessRemoteMarker(*this);
    if (!m_pPed || !m_pPed->HasNativePed())
        return;
    const TICK now = GetTickCount();
    auto& runtime = Runtime(this);
    if (runtime.entryExitDeadline && now >= runtime.entryExitDeadline)
    {
        if (runtime.enteringVehicle)
            OnEnterVehicle();
        else if (runtime.exitingVehicle)
            OnExitVehicle();
        runtime.entryExitDeadline = 0;
        runtime.enteringVehicle = false;
        runtime.exitingVehicle = false;
    }

    const TICK elapsed = now - m_lastUpdate;
    if (m_nState != PLAYER_STATE_ONFOOT && m_nStatus != PLAYER_STATUS_TIMEOUT && elapsed >= TIMEOUT_MS)
        m_nStatus = PLAYER_STATUS_TIMEOUT;

    if (m_nState == PLAYER_STATE_ONFOOT && m_nUpdateType == UPDATE_TYPE_ONFOOT)
    {
        if (m_pPed->IsInVehicle() && !runtime.exitingVehicle)
            OnExitVehicle();

        if (!m_pPed->IsInVehicle())
        {
            SetOnfootTargetSpeedAndPosition(&m_onfootData.m_position, &m_onfootData.m_speed);
            if (!SurfingOnVehicle())
                UpdateOnfootSpeedAndPosition();

            m_pVehicle = nullptr;
            m_nVehicleId = INVALID_ID;
        }
        m_nUpdateType = UPDATE_TYPE_NONE;
    }
    else if (m_nState == PLAYER_STATE_DRIVER && m_nUpdateType == UPDATE_TYPE_INCAR)
    {
        if (m_pVehicle && m_pVehicle->DoesExist())
        {
            if (!runtime.enteringVehicle && (m_pPed->GetVehicle() != m_pVehicle->GetNativeVehicleHandle() || m_pPed->GetVehicleSeatIndex() != 0))
                OnEnterVehicle();

            CMatrix target = CMatrix::Unity();
            m_pVehicle->GetMatrix(&target);
            target.GetPosition() = m_incarData.m_position;
            if (FiniteQuaternion(m_incarData.m_fQuaternion))
            {
                const CVector position = target.GetPosition();
                target.SetRotate(SyncQuaternion(m_incarData.m_fQuaternion));
                target.GetPosition() = position;
            }

            if (IsTrainModel(m_pVehicle->GetModelIndex()))
                UpdateTrain(&target, &m_incarData.m_speed, m_incarData.m_fTrainSpeed);
            else
            {
                SetIncarTargetSpeedAndPosition(&target, &m_incarData.m_position, &m_incarData.m_speed);
                UpdateIncarSpeedAndPosition();
            }

            m_pVehicle->SetHealth(m_incarData.m_fHealth);
            m_pVehicle->EnableSiren(m_incarData.m_bSirenEnabled);
            m_pVehicle->SetLandingGearState(m_incarData.m_bLandingGear ? TRUE : FALSE);

            const ID trailerId = m_incarData.m_nTrailerId;
            CVehicle* trailer = trailerId != 0 && trailerId != INVALID_ID ? ResolveVehicle(trailerId) : nullptr;
            CVehicle* attached = m_pVehicle->GetTrailer();
            if (attached != trailer)
            {
                if (attached)
                {
                    m_pVehicle->SetTrailer(attached);
                    m_pVehicle->DetachTrailer();
                    m_pVehicle->SetTrailer(nullptr);
                }
                if (trailer && trailer->DoesExist())
                {
                    m_pVehicle->SetTrailer(trailer);
                    m_pVehicle->AttachTrailer();
                }
            }
        }
        m_nUpdateType = UPDATE_TYPE_NONE;
    }
    else if (m_nState == PLAYER_STATE_PASSENGER && m_nUpdateType == UPDATE_TYPE_PASSENGER)
    {
        OnEnterVehicle();
        m_nUpdateType = UPDATE_TYPE_NONE;
    }

    // r5 updates pad every frame
    if (m_pVehicle && m_pVehicle->DoesExist() && m_pPed->GetVehicle() == m_pVehicle->GetNativeVehicleHandle())
    {
        const int actualSeat = m_pPed->GetVehicleSeatIndex();
        if (m_nState == PLAYER_STATE_DRIVER && actualSeat == 0)
        {
            const auto& keys = m_incarData.m_controllerState;
            m_pPed->SetKeys(keys.m_value, keys.m_sLeftStickX, keys.m_sLeftStickY);
        }
        else if (m_nState == PLAYER_STATE_PASSENGER && actualSeat == m_nSeatId && actualSeat >= 1 && actualSeat <= 7)
        {
            m_pPed->SetKeys(m_passengerData.m_controllerState.m_value, 0, 0);
        }
    }

    // smooth rotation between packets
    if (m_nState == PLAYER_STATE_DRIVER && m_pVehicle && m_pVehicle->DoesExist() && !IsTrainModel(m_pVehicle->GetModelIndex()))
        UpdateIncarRotation();

    if (m_nState == PLAYER_STATE_ONFOOT && !m_pPed->IsInVehicle())
    {
        ApplyOnfootWeapon(m_pPed, m_onfootData.m_nCurrentWeapon);
        UpdateOnfootRotation();
        ProcessSpecialAction();
        ProcessHead();

        m_pPed->SetKeys(m_onfootData.m_controllerState.m_value, m_onfootData.m_controllerState.m_sLeftStickX, m_onfootData.m_controllerState.m_sLeftStickY);

        if (SurfingOnVehicle())
        {
            if (m_onfootData.m_nSurfingVehicleId < CVehiclePool::MAX_VEHICLES)
            {
                m_pPed->SetCollisionProcessed(FALSE);
                m_pPed->SetCollisionFlag(FALSE);
            }
        }
        else
        {
            const BOOL collision = m_nSpecialAction == SPECIAL_ACTION_ENTER_VEHICLE ? FALSE : TRUE;
            m_pPed->SetCollisionProcessed(collision);
            m_pPed->SetCollisionFlag(collision);
        }

        const bool moving = m_onfootData.m_speed.x != 0.0f || m_onfootData.m_speed.y != 0.0f || m_onfootData.m_speed.z != 0.0f;
        if (!moving)
            m_pPed->SetSpeed(m_onfootData.m_speed);

        if (m_nStatus != PLAYER_STATUS_TIMEOUT)
        {
            const TICK timeout = moving ? IDLE_TIMEOUT_MS : TIMEOUT_MS;
            if (elapsed >= timeout)
                m_nStatus = PLAYER_STATUS_TIMEOUT;
        }

        if (elapsed > TIMEOUT_MS)
        {
            m_onfootData.m_controllerState.m_sLeftStickX = 0;
            m_onfootData.m_controllerState.m_sLeftStickY = 0;
            m_pPed->SetSpeed({});

            CMatrix matrix = CMatrix::Unity();
            m_pPed->GetMatrix(&matrix);
            matrix.GetPosition() = m_onfootData.m_position;
            m_pPed->SetMatrix(matrix);
        }
    }

    if (m_nState == PLAYER_STATE_ONFOOT && !m_pPed->IsInVehicle())
        ProcessSurfing();

    if (m_nStatus == PLAYER_STATUS_TIMEOUT && elapsed < IDLE_TIMEOUT_MS)
        m_nStatus = 0;

    m_bPerformingCustomAnimation = m_pPed->PerformingCustomAnimation();
    if (m_nState != PLAYER_STATE_WASTED && !m_pPed->IsDead())
        m_pPed->SetHealth(100.0f);
}

BOOL CRemotePlayer::Spawn(int a2, int model, int a4, CVector* position, float rotation, D3DCOLOR color, char fightingStyle)
{
    (void)a2;
    (void)a4;

    if (!position || !FiniteVector(*position) || !Finite(rotation))
        return FALSE;

    Remove();

    if (auto* game = g_game.get())
        m_pPed = game->CreatePed(model, *position, rotation, 0, 0);

    if (!m_pPed || !m_pPed->DoesExist())
    {
        DestroyPed(m_pPed);
        m_nState = PLAYER_STATE_NONE;
        return FALSE;
    }

    SetColor(color);

    m_fReportedHealth = 100.0f;
    m_fReportedArmour = 0.0f;
    m_pPed->SetHealth(100.0f);
    m_pPed->SetArmour(0.0f);
    m_pPed->SetFightingStyle(static_cast<std::uint8_t>(fightingStyle));
    m_nState = PLAYER_STATE_SPAWNED;
    m_lastUpdate = GetTickCount();
    m_lastTimestamp = 0;
    m_head.m_lastUpdate = m_head.m_lastLook = m_lastUpdate;
    m_nStatus = 0;
    return TRUE;
}

void CRemotePlayer::Update(Synchronization::OnfootData* data, TICK timestamp)
{
    if (!data || !FiniteVector(data->m_position) || !FiniteVector(data->m_speed) || !FiniteVector(data->m_surfingOffset) || !FiniteQuaternion(data->m_fQuaternion))
        return;
    if (timestamp && m_lastTimestamp && static_cast<std::int32_t>(timestamp - m_lastTimestamp) < 0)
        return;

    m_onfootData = *data;
    m_fReportedHealth = static_cast<float>(data->m_nHealth);
    m_fReportedArmour = static_cast<float>(data->m_nArmor);
    m_animation = data->m_animation;
    m_nSpecialAction = data->m_nSpecialAction;

    const char oldState = static_cast<char>(m_nState);
    ChangeState(oldState, PLAYER_STATE_ONFOOT);
    m_pVehicle = nullptr;
    m_nVehicleId = INVALID_ID;
    m_nUpdateType = UPDATE_TYPE_ONFOOT;
    m_lastUpdate = GetTickCount();
    if (timestamp)
        m_lastTimestamp = timestamp;
}

void CRemotePlayer::Update(Synchronization::IncarData* data, TICK timestamp)
{
    if (!data || data->m_nVehicle >= CVehiclePool::MAX_VEHICLES || !FiniteVector(data->m_position) || !FiniteVector(data->m_speed) || !FiniteQuaternion(data->m_fQuaternion) ||
        !Finite(data->m_fHealth))
        return;
    if (timestamp && m_lastTimestamp && static_cast<std::int32_t>(timestamp - m_lastTimestamp) < 0)
        return;

    m_incarData = *data;
    m_nVehicleId = data->m_nVehicle;
    m_pVehicle = ResolveVehicle(m_nVehicleId);
    m_nSeatId = 0;
    if (m_pVehicle)
    {
        m_pVehicle->m_bUnoccupiedSync = FALSE;
        m_pVehicle->m_bRemoteUnocSync = FALSE;
        if (auto* vehiclePool = g_netGame ? g_netGame->GetVehiclePool() : nullptr)
            vehiclePool->m_nLastUndrivenId[m_nVehicleId] = CVehiclePool::INVALID_ID;
    }
    m_fReportedHealth = static_cast<float>(data->m_nDriverHealth);
    m_fReportedArmour = static_cast<float>(data->m_nDriverArmor);

    const char oldState = static_cast<char>(m_nState);
    m_nUpdateType = UPDATE_TYPE_INCAR;
    m_lastUpdate = GetTickCount();
    if (timestamp)
        m_lastTimestamp = timestamp;
    m_nStatus = 0;
    ChangeState(oldState, PLAYER_STATE_DRIVER);
}

void CRemotePlayer::Update(Synchronization::PassengerData* data, TICK timestamp)
{
    if (!data || data->m_nVehicleId >= CVehiclePool::MAX_VEHICLES || (data->m_nSeatId & 0x3Fu) == 0 || (data->m_nSeatId & 0x3Fu) > 7 || !FiniteVector(data->m_position))
        return;

    m_passengerData = *data;
    m_nVehicleId = data->m_nVehicleId;
    m_pVehicle = ResolveVehicle(m_nVehicleId);
    m_nSeatId = data->m_nSeatId & 0x3F;
    m_bPassengerDriveBy = (data->m_nSeatId & 0x40) != 0;
    m_fReportedHealth = static_cast<float>(data->m_nHealth);
    m_fReportedArmour = static_cast<float>(data->m_nArmor);

    const char oldState = static_cast<char>(m_nState);
    m_nUpdateType = UPDATE_TYPE_PASSENGER;
    m_lastUpdate = GetTickCount();
    (void)timestamp;
    m_nStatus = 0;
    ChangeState(oldState, PLAYER_STATE_PASSENGER);
}

void CRemotePlayer::Remove()
{
    if (m_marker)
    {
        if (auto* game = g_game.get())
            game->DeleteMarker(m_marker);
        m_marker = 0;
    }
    Runtime(this).markerColor = 0;
    DestroyPed(m_pPed);
    m_pVehicle = nullptr;
    m_nVehicleId = INVALID_ID;
    m_nState = PLAYER_STATE_NONE;
    m_bDrawLabels = TRUE;
    m_bMarkerState = FALSE;
    m_markerPosition = {};
    ResetData();
}

void CRemotePlayer::Kill()
{
    if (m_pPed && m_pPed->HasNativePed())
    {
        m_pPed->SetKeys(0, 0, 0);
        m_pPed->SetHealth(0.0f);
    }

    m_nState = PLAYER_STATE_WASTED;
    ResetData();
}

void CRemotePlayer::Chat(const char* text)
{
    if (!text || !*text || !g_chat)
        return;

    auto* netGame = g_netGame.get();
    auto* playerPool = netGame ? netGame->GetPlayerPool() : nullptr;
    const char* name = playerPool ? playerPool->GetName(m_nId) : nullptr;
    if (!name || !*name)
        name = "Player";
    g_chat->AddChatMessage(name, GetColorAsARGB(), text);
}

BOOL CRemotePlayer::DoesExist()
{
    return m_pPed && m_pPed->DoesExist();
}
