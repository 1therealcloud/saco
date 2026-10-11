#include <stdafx.h>
#include "main.h"
#include "net/CNetGame.h"
#include "players/PlayerColors.h"

#include "CLocalPlayer.h"
#include "CRemotePlayer.h"

#include "game/CCamera.h"
#include "game/CAudio.h"
#include "gui/CDialog.h"
#include "game/CGame.h"
#include "game/InputHandler.h"
#include "gui/CInput.h"
#include "config/CSettings.h"
#include "game/CPad.h"
#include "game/Scripting.h"
#include "sync/KeyStuff.h"
#include "sync/AimStuff.h"
#include "game/SpecialAction.h"
#include "game/entities/CObject.h"
#include "game/entities/CPed.h"
#include "game/entities/CVehicle.h"
#include "render/render.h"
#include "pools/CObjectPool.h"
#include "pools/CVehiclePool.h"
#include "pools/CMenuPool.h"
#include "utils/math/CQuaternion.h"

#include <BitStream.h>
#include <PacketEnumerations.h>
#include <RakClient.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>

namespace
{
    D3DCOLOR g_localColor = 0xFFFFFFFFu;
    int g_incarSendrate = 30;
    int g_onfootSendrate = 30;
    int g_firingSendrate = 30;
    int g_sendMultiplier = 2;

    constexpr std::uint32_t HEAD_UPDATE_MS = 1000;
    constexpr std::uint32_t HEAD_LOOK_MS = 200;
    constexpr std::uint32_t CAMERA_TARGET_UPDATE_MS = 100;
    constexpr std::uint32_t STATS_UPDATE_MS = 1000;
    constexpr std::uint32_t SURFING_REACQUIRE_MS = 2000;

    bool FiniteVector(const CVector& v)
    {
        return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
    }

    void ResetCameraTarget(CLocalPlayer::CameraTarget& target)
    {
        target.m_nObject = CLocalPlayer::INVALID_ID;
        target.m_nVehicle = CLocalPlayer::INVALID_ID;
        target.m_nPlayer = CLocalPlayer::INVALID_ID;
        target.m_nActor = CLocalPlayer::INVALID_ID;
    }

    CVector PositionOf(const CEntity* entity)
    {
        CMatrix matrix = CMatrix::Unity();
        if (entity)
            entity->GetMatrix(&matrix);
        return matrix.GetPosition();
    }

    template <class T>
    bool SendSyncPacket(std::uint8_t packetId, const T& data)
    {
        if (!g_netGame || g_netGame->GetState() != CNetGame::GAME_MODE_CONNECTED)
            return false;
        auto* client = g_netGame->GetRakClient();
        if (!client)
            return false;

        RakNet::BitStream bs;
        bs.Write(packetId);
        bs.Write(reinterpret_cast<const char*>(&data), sizeof(data));
        return client->Send(&bs, HIGH_PRIORITY, UNRELIABLE_SEQUENCED, 0);
    }

    std::uint8_t HealthByte(float value)
    {
        return std::isfinite(value) ? static_cast<std::uint8_t>(std::clamp(value, 0.0f, 255.0f)) : 0;
    }

    void SaveRotation(const CMatrix& matrix, float (&quaternion)[4])
    {
        CQuaternion rotation;
        rotation.Set(*reinterpret_cast<const RwMatrix*>(&matrix));
        rotation.Normalise();
        quaternion[0] = rotation.w;
        quaternion[1] = rotation.x;
        quaternion[2] = rotation.y;
        quaternion[3] = rotation.z;
    }

    bool FiniteRotation(const float (&quaternion)[4])
    {
        return std::all_of(std::begin(quaternion), std::end(quaternion), [](float value) {
            return std::isfinite(value);
        });
    }

    std::uint8_t GetExtraKeyState(const CPad& pad)
    {
        if (pad.NewState.DPadRight)
            return 1;
        if (pad.NewState.DPadLeft)
            return 2;
        if (pad.NewState.DPadDown)
            return 3;
        return 0;
    }


}

int& CLocalPlayer::RefIncarSendrate()
{
    return g_incarSendrate;
}

int& CLocalPlayer::RefOnfootSendrate()
{
    return g_onfootSendrate;
}

int& CLocalPlayer::RefFiringSendrate()
{
    return g_firingSendrate;
}

int& CLocalPlayer::RefSendMultiplier()
{
    return g_sendMultiplier;
}

CLocalPlayer::CLocalPlayer()
{
    std::memset(this, 0, sizeof(*this));
    const TICK now = GetTickCount();
    m_lastSelectionTick = now;
    m_head.m_lastUpdate = now;
    m_head.m_lastLook = now;
    m_lastWeaponsUpdate = now;
    m_lastCameraTargetUpdate = now;

    if (auto* game = g_game.get())
        m_pPed = game->GetPlayerPed();

    m_bIsActive = FALSE;
    m_bIsWasted = FALSE;
    field_10d = 0;
    m_zoneDisplayingEnd = 0;
    m_lastUpdate = now;
    m_lastSpecUpdate = now;
    m_lastAnyUpdate = now;
    m_lastAimUpdate = now;
    m_lastStatsUpdate = now;
    m_bDoesSpectating = FALSE;
    m_spectating.m_nMode = 0;
    m_spectating.m_nType = 0;
    m_spectating.m_nObject = -1;
    m_spectating.m_bProcessed = FALSE;

    ResetData();
    m_nTeam = 0xFF;
}

CPed* CLocalPlayer::GetPed()
{
    return m_pPed;
}

void CLocalPlayer::ResetData()
{
    std::memset(&m_incarData, 0, sizeof(m_incarData));
    std::memset(&m_aimData, 0, sizeof(m_aimData));
    std::memset(&m_trailerData, 0, sizeof(m_trailerData));
    std::memset(&m_onfootData, 0, sizeof(m_onfootData));
    std::memset(&m_passengerData, 0, sizeof(m_passengerData));

    m_bHasSpawnInfo = FALSE;
    m_classSelection.m_bEnableAfterDeath = FALSE;
    m_classSelection.m_bIsActive = FALSE;
    m_classSelection.m_bWaitingForSpawnRequestReply = FALSE;
    m_nCurrentInterior = 0;
    m_nLastVehicle = INVALID_ID;
    m_nCurrentVehicle = INVALID_ID;
    m_bPassengerDriveBy = FALSE;
    m_bClearedToSpawn = FALSE;
    m_classSelection.m_nSelected = 0;
    m_bInRCMode = FALSE;
    m_damage.m_nVehicleUpdating = INVALID_ID;
    ResetCameraTarget(m_cameraTarget);
    m_head.m_direction = CVector{};
    m_animation = {};
    field_100 = 0;

    std::memset(&m_weaponsData, 0, sizeof(m_weaponsData));
    *reinterpret_cast<ID*>(&m_weaponsData.data[0]) = INVALID_ID;
    *reinterpret_cast<ID*>(&m_weaponsData.data[2]) = INVALID_ID;

    const TICK now = GetTickCount();
    m_lastWeaponsUpdate = now;
    m_lastCameraTargetUpdate = now;
    m_lastAimUpdate = now;
    m_lastAnyUpdate = now;
    m_surfing.m_pVehicle = nullptr;
    m_surfing.m_bStuck = FALSE;
    m_surfing.m_bIsActive = FALSE;
    m_surfing.m_nEntityId = INVALID_ID;
    m_surfing.m_position = CVector{};
    m_surfing.m_nMode = SURFING_MODE_NONE;
    m_surfing.m_lastUpdate = now;
    m_surfing.field_1e = 0;
    m_damage.m_nBumper = 0;
    m_damage.m_nDoor = 0;
    m_damage.m_bLight = false;
    m_damage.m_bWheel = false;
}

void CLocalPlayer::ProcessHead()
{
    if (!m_pPed || !m_pPed->HasNativePed() || !g_game || !g_game->DoesHeadMoves() || m_bDoesSpectating || m_classSelection.m_bIsActive || m_pPed->IsDead() ||
        (g_input && g_input->IsEnabled()))
        return;

    // R5 skips the head task while the ped is holding stuff
    if (m_pPed->IsHoldingStuff())
        return;

    const CVector position = PositionOf(m_pPed);
    if (!FiniteVector(position))
        return;
    const TICK now = GetTickCount();
    if (now - m_head.m_lastUpdate > HEAD_UPDATE_MS)
    {
        const AimStuff::Aim snapshot = *AimStuff::GetAim();
        if (!FiniteVector(snapshot.front))
            return;
        const CVector direction = snapshot.front * 20.0f;
        if (!FiniteVector(direction))
            return;
        m_head.m_direction = direction;
        m_head.m_lastUpdate = now;
    }

    if (now - m_head.m_lastLook > HEAD_LOOK_MS)
    {
        const CVector target = position + m_head.m_direction;
        if (!FiniteVector(target))
            return;
        m_pPed->LookAt(target);
        m_head.m_lastLook = now;
    }
}

void CLocalPlayer::SetSpecialAction(char id)
{
    if (!m_pPed)
        return;

    switch (static_cast<unsigned char>(id))
    {
        case SPECIAL_ACTION_NONE:
            if (m_pPed->DoesDancing())
                m_pPed->StopDancing();
            if (m_pPed->UsingCellphone())
                m_pPed->EnableCellphone(FALSE);
            if (m_pPed->DoesUrinating())
                m_pPed->StopUrinating();
            break;
        case SPECIAL_ACTION_DANCE1:
        case SPECIAL_ACTION_DANCE2:
        case SPECIAL_ACTION_DANCE3:
        case SPECIAL_ACTION_DANCE4:
            if (!m_pPed->GetVehicle())
                m_pPed->StartDancing(id - SPECIAL_ACTION_DANCE1);
            break;
        case SPECIAL_ACTION_HANDSUP:
            if (!m_pPed->GetVehicle())
                m_pPed->HandsUp();
            break;
        case SPECIAL_ACTION_USECELLPHONE:
            if (!m_pPed->GetVehicle())
                m_pPed->EnableCellphone(TRUE);
            break;
        case SPECIAL_ACTION_STOPUSECELLPHONE:
            if (m_pPed->UsingCellphone())
                m_pPed->EnableCellphone(FALSE);
            break;
        case SPECIAL_ACTION_URINATING:
            if (!m_pPed->GetVehicle())
                m_pPed->StartUrinating();
            break;
        default:
            break;
    }
}

char CLocalPlayer::GetSpecialAction()
{
    if (!m_pPed)
        return SPECIAL_ACTION_NONE;
    if (m_pPed->DoesDancing())
        return static_cast<char>(SPECIAL_ACTION_DANCE1 + std::clamp(m_pPed->m_nDanceStyle, 0u, 3u));
    if (m_pPed->DoesHandsUp())
        return SPECIAL_ACTION_HANDSUP;
    if (m_pPed->UsingCellphone())
        return SPECIAL_ACTION_USECELLPHONE;
    if (m_pPed->DoesUrinating())
        return SPECIAL_ACTION_URINATING;
    return SPECIAL_ACTION_NONE;
}

void CLocalPlayer::UpdateSurfing()
{
    if (!m_pPed || !m_surfing.m_pVehicle)
        return;

    const CVector playerPosition = PositionOf(m_pPed);
    const CVector entityPosition = PositionOf(reinterpret_cast<CEntity*>(m_surfing.m_pVehicle));
    m_surfing.m_position = playerPosition - entityPosition;
    m_surfing.m_lastUpdate = GetTickCount();
}

void CLocalPlayer::SetSurfing(CVehicle* vehicle, BOOL stuck)
{
    m_surfing.m_pVehicle = vehicle;
    m_surfing.m_bStuck = stuck;
}

void CLocalPlayer::ProcessSurfing()
{
    auto* pools = g_netGame ? g_netGame->m_pPools : nullptr;
    auto* vehiclePool = pools ? pools->m_pVehicle : nullptr;
    auto* objectPool = pools ? pools->m_pObject : nullptr;

    if (!m_pPed)
        return;

    void* contact = m_pPed->GetContactEntity();
    int contactType = 0;
    ID entityId = INVALID_ID;
    CEntity* entity = nullptr;

    if (contact && vehiclePool)
    {
        const ID id = vehiclePool->Find(contact);
        if (id != CVehiclePool::INVALID_ID)
        {
            entity = vehiclePool->Get(id);
            entityId = id;
            contactType = 1;
        }
    }

    if (!entity && contact && objectPool)
    {
        const int id = objectPool->GetId(contact);
        if (id != CObjectPool::INVALID_ID)
        {
            entity = objectPool->Get(static_cast<ID>(id));
            entityId = static_cast<ID>(2000 + id);
            contactType = 2;
        }
    }

    if (!m_pPed->IsOnGround() || m_pPed->GetVehicle() || m_pPed->IsDead())
    {
        m_surfing.m_nMode = SURFING_MODE_NONE;
        m_surfing.m_bIsActive = FALSE;
        m_surfing.m_nEntityId = INVALID_ID;
        m_surfing.m_position = CVector{};
        m_surfing.m_pVehicle = nullptr;
        m_surfing.m_bStuck = FALSE;
        return;
    }

    const TICK now = GetTickCount();
    if (!entity)
    {
        if (m_surfing.m_bIsActive && now - m_surfing.m_lastUpdate >= SURFING_REACQUIRE_MS)
        {
            m_surfing.m_bIsActive = FALSE;
            m_surfing.m_position = CVector{};
            m_surfing.m_nEntityId = INVALID_ID;
            m_surfing.m_pVehicle = nullptr;
            m_surfing.m_bStuck = FALSE;
            m_surfing.m_nMode = SURFING_MODE_NONE;
        }
        return;
    }

    const bool changedEntity = m_surfing.m_nEntityId != entityId;
    m_surfing.m_nEntityId = entityId;
    m_surfing.m_lastUpdate = now;
    if (!m_surfing.m_bIsActive || changedEntity)
    {
        m_surfing.m_pVehicle = reinterpret_cast<CVehicle*>(entity);
        m_surfing.m_bStuck = FALSE;
        UpdateSurfing();
    }
    m_surfing.m_pVehicle = reinterpret_cast<CVehicle*>(entity);
    m_surfing.m_bStuck = TRUE;
    m_surfing.m_bIsActive = TRUE;
    m_surfing.m_nMode = contactType == 1 || contactType == 2 ? SURFING_MODE_FIXED : SURFING_MODE_NONE;
}

BOOL CLocalPlayer::NeedsToUpdate(const void* oldData, const void* newData, unsigned int len)
{
    const TICK now = GetTickCount();
    if (now - m_lastAnyUpdate > 500 || !oldData || !newData || std::memcmp(oldData, newData, len) != 0)
    {
        m_lastAnyUpdate = now;
        return TRUE;
    }
    return FALSE;
}

int CLocalPlayer::GetIncarSendRate()
{
    if (!m_pPed)
        return 1000;
    if (g_netGame && g_netGame->LanMode())
        return 15;
    const int affected = 0;
    return RefIncarSendrate() + std::max(affected, 0) * RefSendMultiplier();
}

int CLocalPlayer::GetOnfootSendRate()
{
    if (!m_pPed)
        return 1000;
    if (g_netGame && g_netGame->LanMode())
        return 15;
    const int affected = 0;
    return RefOnfootSendrate() + std::max(affected, 0) * RefSendMultiplier();
}

int CLocalPlayer::GetUnoccupiedSendRate()
{
    if (!m_pPed)
        return 1000;
    return m_pPed->GetVehicle() ? GetIncarSendRate() : GetOnfootSendRate();
}

void CLocalPlayer::SetSpawnInfo(const SpawnInfo* info)
{
    if (!info || !FiniteVector(info->m_position) || !std::isfinite(info->m_fRotation))
        return;
    m_spawnInfo = *info;
    m_bHasSpawnInfo = TRUE;
}

BOOL CLocalPlayer::Spawn()
{
    if (!m_bHasSpawnInfo || !g_game)
        return FALSE;

    const CVector position = m_spawnInfo.m_position;
    const float rotation = m_spawnInfo.m_fRotation;
    if (!FiniteVector(position) || !std::isfinite(rotation))
        return FALSE;

    auto* camera = g_game->GetCamera();
    auto* ped = g_game->GetPlayerPed();
    if (!camera || !ped)
        return FALSE;

    ped->ResetPointers();
    if (!ped->HasNativePed() || !ped->IsValid())
        return FALSE;

    // gta's automatic resurrection is disabled with CGameLogic::Update.
    if (ped->IsDead())
    {
        using RestorePlayerFn = void(__cdecl*)(void*, CVector, float);
        reinterpret_cast<RestorePlayerFn>(0x442060)(ped->GetNativePedHandle(), position, rotation);
        ped->ResetPointers();
        if (!ped->HasNativePed() || !ped->IsValid() || ped->IsDead())
            return FALSE;
    }

    m_pPed = ped;

    render::ShowClassSelection(false);
    render::SetCursorMode(CURSOR_NONE, false);

    camera->Detach();
    camera->Restore();
    g_game->EnableHUD(TRUE);
    ped->SetControllable(TRUE);

    static bool firstSpawn = true;
    if (firstSpawn)
        firstSpawn = false;
    else
        ped->SetInitialState();

    g_game->RefreshRenderer(position.x, position.y);
    ped->SetSpawnInfo(&position, rotation);
    ped->SetModelIndex(m_spawnInfo.m_nSkin);
    ped->ResetDamageEntity();
    SetSpecialAction(SPECIAL_ACTION_NONE);

    m_nTeam = m_spawnInfo.m_nTeam;
    m_surfing.m_position = CVector{};
    m_surfing.m_nEntityId = INVALID_ID;
    m_surfing.m_pVehicle = nullptr;
    m_surfing.m_bStuck = FALSE;
    m_surfing.m_bIsActive = FALSE;
    m_surfing.m_nMode = SURFING_MODE_NONE;
    m_surfing.m_lastUpdate = GetTickCount();
    m_surfing.field_1e = 0;

    g_game->DisableTrainTraffic();
    ped->LinkToInterior(m_nCurrentInterior, TRUE);
    ped->Teleport(CVector{position.x, position.y, position.z + 0.5f});
    ped->SetRotation(rotation);

    m_bDoesSpectating = FALSE;
    m_spectating.m_nMode = 0;
    m_spectating.m_nType = SPEC_TYPE_NONE;
    m_spectating.m_nObject = -1;
    m_spectating.m_bProcessed = FALSE;

    m_bIsWasted = FALSE;
    m_bIsActive = TRUE;
    m_classSelection.m_bIsActive = FALSE;
    m_classSelection.m_bWaitingForSpawnRequestReply = FALSE;
    m_classSelection.m_bEnableAfterDeath = FALSE;
    m_zoneDisplayingEnd = GetTickCount() + 1000;
    m_lastUpdate = 0;
    m_lastAimUpdate = 0;
    m_lastAnyUpdate = 0;

    if (g_netGame)
        g_netGame->SendSpawn();

    return TRUE;
}

void CLocalPlayer::SetColor(D3DCOLOR color)
{
    g_localColor = color;
    const auto* pool = g_netGame ? g_netGame->GetPlayerPool() : nullptr;
    if (pool && pool->m_localInfo.m_nId < PlayerColors::COUNT)
        PlayerColors::SetColor(pool->m_localInfo.m_nId, color);
}

D3DCOLOR CLocalPlayer::GetColorAsRGBA()
{
    const auto* pool = g_netGame ? g_netGame->GetPlayerPool() : nullptr;
    if (pool && pool->m_localInfo.m_nId < PlayerColors::COUNT)
        return PlayerColors::GetColor(pool->m_localInfo.m_nId);
    return g_localColor;
}

D3DCOLOR CLocalPlayer::GetColorAsARGB()
{
    const D3DCOLOR rgba = GetColorAsRGBA();
    return (rgba >> 8) | (rgba << 24);
}

void CLocalPlayer::ProcessOnfootWorldBounds()
{
    if (!m_pPed || !g_netGame || !g_netGame->m_pSettings)
        return;
    const auto& bounds = g_netGame->m_pSettings->m_fWorldBoundaries;
    m_pPed->EnforceWorldBoundries(bounds[0], bounds[1], bounds[2], bounds[3]);
}

void CLocalPlayer::ProcessIncarWorldBounds()
{
    auto* pools = g_netGame ? g_netGame->m_pPools : nullptr;
    auto* vehiclePool = pools ? pools->m_pVehicle : nullptr;

    if (!m_pPed || !vehiclePool || !g_netGame || !g_netGame->m_pSettings)
        return;
    const auto id = vehiclePool->Find(m_pPed->GetVehicle());
    auto* vehicle = id != CVehiclePool::INVALID_ID ? vehiclePool->Get(id) : nullptr;
    if (!vehicle)
        return;
    const auto& bounds = g_netGame->m_pSettings->m_fWorldBoundaries;
    vehicle->EnforceWorldBoundries(bounds[0], bounds[1], bounds[2], bounds[3]);
}

void CLocalPlayer::RequestSpawn()
{
    if (!m_classSelection.m_bIsActive || !m_bClearedToSpawn || !m_bHasSpawnInfo || m_classSelection.m_bWaitingForSpawnRequestReply)
        return;

    if (g_netGame && g_netGame->RequestSpawn())
    {
        m_classSelection.m_bWaitingForSpawnRequestReply = TRUE;
        m_lastSelectionTick = GetTickCount();
    }
}

void CLocalPlayer::PrepareForClassSelection()
{
    m_bClearedToSpawn = FALSE;
    m_classSelection.m_bIsActive = TRUE;
    m_classSelection.m_bWaitingForSpawnRequestReply = FALSE;
    if (m_pPed)
    {
        m_pPed->SetHealth(100.0f);
        m_pPed->SetControllable(FALSE);
    }
    m_initialSelectionTick = GetTickCount();
    m_lastSelectionTick = GetTickCount();
}

void CLocalPlayer::PrepareForClassSelection_Outcome(BOOL outcome)
{
    m_classSelection.m_bWaitingForSpawnRequestReply = FALSE;
    m_bClearedToSpawn = outcome ? TRUE : FALSE;

    if (!outcome || !m_bHasSpawnInfo || !m_pPed)
        return;

    m_nTeam = m_spawnInfo.m_nTeam;
    m_pPed->SetControllable(FALSE);
    m_pPed->SetSpawnInfo(&m_spawnInfo.m_position, m_spawnInfo.m_fRotation);
    m_pPed->SetModelIndex(m_spawnInfo.m_nSkin);
    m_pPed->ForceRotation(m_spawnInfo.m_fRotation);
    m_pPed->SetHealth(100.0f);
    m_pPed->SetArmour(0.0f);
    m_lastSelectionTick = GetTickCount();
}

void CLocalPlayer::EnableSpectating(BOOL enable)
{
    if (!!enable == !!m_bDoesSpectating)
        return;

    if (m_classSelection.m_bEnableAfterDeath && !enable)
    {
        m_bIsActive = FALSE;
        m_bHasSpawnInfo = FALSE;
    }

    m_bDoesSpectating = enable;
    m_spectating.m_nMode = 0;
    m_spectating.m_nType = SPEC_TYPE_NONE;
    m_spectating.m_nObject = -1;
    m_spectating.m_bProcessed = FALSE;

    if (!enable)
        return;

    if (m_pPed && m_pPed->HasNativePed())
        m_pPed->SetControllable(FALSE);

    auto* camera = g_game ? g_game->GetCamera() : nullptr;
    if (!camera)
        return;

    camera->Set(CVector{50.0f, 50.0f, 50.0f}, CVector{});
    camera->PointAt(CVector{60.0f, 60.0f, 50.0f}, 2);

    m_spectating.m_bProcessed = TRUE;
}

void CLocalPlayer::SpectateForVehicle(ID id)
{
    auto* pools = g_netGame ? g_netGame->m_pPools : nullptr;
    auto* vehiclePool = pools ? pools->m_pVehicle : nullptr;

    if (!vehiclePool || id >= CVehiclePool::MAX_VEHICLES || !vehiclePool->DoesExist(id))
        return;
    m_spectating.m_nType = SPEC_TYPE_VEHICLE;
    m_spectating.m_nObject = id;
    m_spectating.m_bProcessed = FALSE;
}

void CLocalPlayer::SpectateForPlayer(ID id)
{
    auto* playerPool = g_netGame ? g_netGame->GetPlayerPool() : nullptr;
    auto* remote = playerPool ? playerPool->GetPlayer(id) : nullptr;
    if (!remote || !remote->m_pPed)
        return;
    m_spectating.m_nType = SPEC_TYPE_PLAYER;
    m_spectating.m_nObject = id;
    m_spectating.m_bProcessed = FALSE;
}

BOOL CLocalPlayer::NeedsToSendOnfootData(short controllerState, short leftStickY, short leftStickX)
{
    return controllerState != m_onfootData.m_controllerState.m_value || leftStickY != m_onfootData.m_controllerState.m_sLeftStickY ||
           leftStickX != m_onfootData.m_controllerState.m_sLeftStickX;
}

BOOL CLocalPlayer::NeedsToSendIncarData(short controllerState, short leftStickX, short leftStickY)
{
    return controllerState != m_incarData.m_controllerState.m_value || leftStickX != m_incarData.m_controllerState.m_sLeftStickX ||
           leftStickY != m_incarData.m_controllerState.m_sLeftStickY;
}

bool CLocalPlayer::DefineCameraTarget(CameraTarget* info)
{
    if (!info)
        return false;
    ResetCameraTarget(*info);
    return false;
}

void CLocalPlayer::UpdateCameraTarget()
{
    const TICK now = GetTickCount();
    if (now - m_lastCameraTargetUpdate <= CAMERA_TARGET_UPDATE_MS)
        return;

    CameraTarget target{};
    ResetCameraTarget(target);
    DefineCameraTarget(&target);
    if (std::memcmp(&target, &m_cameraTarget, sizeof(target)) != 0)
    {
        m_cameraTarget = target;
    }
    m_lastCameraTargetUpdate = now;
}

void CLocalPlayer::DrawCameraTargetLabel()
{}

void CLocalPlayer::SendUnoccupiedData(ID vehicleId, char seat)
{
    auto* pool = g_netGame ? g_netGame->GetVehiclePool() : nullptr;
    auto* vehicle = pool ? pool->Get(vehicleId) : nullptr;
    if (!vehicle || !vehicle->HasNativeVehicle())
        return;

    CMatrix matrix = CMatrix::Unity();
    vehicle->GetMatrix(&matrix);

    Synchronization::UnoccupiedData data{};
    data.m_nVehicleId = vehicleId;
    data.m_nSeatId = static_cast<std::uint8_t>(seat);
    data.m_roll = matrix.GetRight();
    data.m_direction = matrix.GetForward();
    data.m_position = matrix.GetPosition();
    vehicle->GetSpeed(&data.m_speed);
    vehicle->GetTurnSpeed(&data.m_turnSpeed);
    data.m_fHealth = vehicle->GetHealth();

    if (!FiniteVector(data.m_roll) || !FiniteVector(data.m_direction) || !FiniteVector(data.m_position) ||
        !FiniteVector(data.m_speed) || !FiniteVector(data.m_turnSpeed) || !std::isfinite(data.m_fHealth))
        return;

    if (SendSyncPacket(ID_UNOCCUPIED_SYNC, data))
    {
        vehicle->m_bUnoccupiedSync = TRUE;
        vehicle->m_bRemoteUnocSync = FALSE;
        if (auto* players = g_netGame->GetPlayerPool())
            pool->m_nLastUndrivenId[vehicleId] = players->m_localInfo.m_nId;
    }
}

void CLocalPlayer::SendOnfootData()
{
    if (!m_pPed || !m_pPed->HasNativePed())
        return;

    Synchronization::OnfootData data{};
    short leftStickX = 0, leftStickY = 0;
    data.m_controllerState.m_value = m_pPed->GetKeys(&leftStickX, &leftStickY);
    data.m_controllerState.m_sLeftStickX = leftStickX;
    data.m_controllerState.m_sLeftStickY = leftStickY;

    CPad pad{};
    std::uint8_t extraKeys = 0;
    if (const auto* internalKeys = KeyStuff::GetInternalKeys())
    {
        std::memcpy(&pad, internalKeys, sizeof(pad));
        extraKeys = GetExtraKeyState(pad);
    }

    CMatrix matrix = CMatrix::Unity();
    m_pPed->GetMatrix(&matrix);
    data.m_position = matrix.GetPosition();

    SaveRotation(matrix, data.m_fQuaternion);

    constexpr float QUATERNION_EPSILON = 0.00001f;
    bool unchangedRotation = true;
    for (std::size_t i = 0; i < 4; ++i)
        unchangedRotation = unchangedRotation && std::fabs(data.m_fQuaternion[i] - m_onfootData.m_fQuaternion[i]) < QUATERNION_EPSILON;
    if (unchangedRotation)
        std::memcpy(data.m_fQuaternion, m_onfootData.m_fQuaternion, sizeof(data.m_fQuaternion));

    data.m_nCurrentWeapon = Synchronization::PackWeaponAndKeys(m_pPed->GetCurrentWeapon(), extraKeys);
    data.m_nSpecialAction = static_cast<std::uint8_t>(GetSpecialAction());
    data.m_nHealth = HealthByte(m_pPed->GetHealth());
    data.m_nArmor = HealthByte(m_pPed->GetArmour());
    m_pPed->GetSpeed(&data.m_speed);

    if (m_surfing.m_bIsActive)
    {
        data.m_surfingOffset = m_surfing.m_position;
        data.m_nSurfingVehicleId = m_surfing.m_nEntityId;
    }
    else
    {
        data.m_surfingOffset = CVector{};
        data.m_nSurfingVehicleId = 0;
    }
    data.m_animation = m_animation;
    field_100 = 1;

    if (!FiniteVector(data.m_position) || !FiniteVector(data.m_speed) || !FiniteVector(data.m_surfingOffset) || !FiniteRotation(data.m_fQuaternion))
        return;
    if (m_nCurrentVehicle == INVALID_ID && !NeedsToUpdate(&m_onfootData, &data, sizeof(data)))
        return;
    if (!SendSyncPacket(ID_PLAYER_SYNC, data))
        return;
    m_onfootData = data;
    m_nCurrentVehicle = INVALID_ID;
    m_lastAnyUpdate = GetTickCount();
}

void CLocalPlayer::SendAimData()
{
    if (!m_pPed || !m_pPed->HasNativePed())
        return;

    const auto* aim = AimStuff::GetAim();
    if (!aim || !FiniteVector(aim->front) || !FiniteVector(aim->source))
        return;

    Synchronization::AimData data{};
    data.m_nCameraMode = static_cast<std::uint8_t>(AimStuff::GetCameraMode());
    data.m_aimf1 = aim->front;
    data.m_aimPos = aim->source;
    data.m_fAimZ = m_pPed->GetAimZ();

    const float zoom = AimStuff::GetCameraExtZoom();
    const float aspect = AimStuff::GetAspectRatio();
    if (!std::isfinite(data.m_fAimZ) || !std::isfinite(zoom) || !std::isfinite(aspect))
        return;

    data.m_nCameraExtZoom = Synchronization::CompressCameraExtZoom(std::clamp(zoom, 0.0f, 1.0f));
    data.m_nAspectRatio = Synchronization::CompressAspectRatio(aspect);

    if (SendSyncPacket(ID_AIM_SYNC, data))
    {
        m_aimData = data;
        m_lastAimUpdate = GetTickCount();
    }
}

void CLocalPlayer::SendTrailerData(ID trailerId)
{
    if (!g_netGame)
        return;

    auto* pool = g_netGame->GetVehiclePool();
    auto* trailer = pool ? pool->Get(trailerId) : nullptr;
    if (!trailer || !trailer->HasNativeVehicle())
        return;

    Synchronization::TrailerData data{};
    data.m_nId = trailerId;
    CMatrix matrix = CMatrix::Unity();
    trailer->GetMatrix(&matrix);
    SaveRotation(matrix, data.m_fQuaternion);
    data.m_position = matrix.GetPosition();
    trailer->GetSpeed(&data.m_speed);
    trailer->GetTurnSpeed(&data.m_turnSpeed);

    if (!FiniteVector(data.m_position) || !FiniteVector(data.m_speed) || !FiniteVector(data.m_turnSpeed) || !FiniteRotation(data.m_fQuaternion))
        return;
    if (SendSyncPacket(ID_TRAILER_SYNC, data))
        m_trailerData = data;
}

void CLocalPlayer::SendPassengerData()
{
    if (!m_pPed || !m_pPed->HasNativePed() || !g_netGame)
        return;

    auto* vehiclePool = g_netGame->GetVehiclePool();
    const ID vehicleId = vehiclePool ? vehiclePool->Find(m_pPed->GetVehicle()) : INVALID_ID;
    const int seat = m_pPed->GetVehicleSeatIndex();
    if (vehicleId == INVALID_ID || seat < 1 || seat > 63)
        return;

    Synchronization::PassengerData data{};
    data.m_nVehicleId = vehicleId;
    data.m_nSeatId = static_cast<std::uint8_t>((seat & 0x3F) | (m_bPassengerDriveBy ? 0x40 : 0));
    data.m_nHealth = HealthByte(m_pPed->GetHealth());
    data.m_nArmor = HealthByte(m_pPed->GetArmour());

    short leftX = 0, leftY = 0;
    data.m_controllerState.m_value = m_pPed->GetKeys(&leftX, &leftY);
    data.m_controllerState.m_sLeftStickX = leftX;
    data.m_controllerState.m_sLeftStickY = leftY;

    CPad pad{};
    std::uint8_t extraKeys = 0;
    if (const auto* internalKeys = KeyStuff::GetInternalKeys())
    {
        std::memcpy(&pad, internalKeys, sizeof(pad));
        extraKeys = GetExtraKeyState(pad);
    }
    data.m_nCurrentWeapon = Synchronization::PackWeaponAndKeys(m_pPed->GetCurrentWeapon(), extraKeys);

    CMatrix matrix = CMatrix::Unity();
    m_pPed->GetMatrix(&matrix);
    data.m_position = matrix.GetPosition();
    if (!FiniteVector(data.m_position) || !SendSyncPacket(ID_PASSENGER_SYNC, data))
        return;

    m_passengerData = data;
    m_nCurrentVehicle = vehicleId;
}

void CLocalPlayer::WastedNotification()
{}

void CLocalPlayer::RequestClass(int id)
{
    if (g_netGame)
        g_netGame->RequestClass(static_cast<std::uint16_t>(id));
}

void CLocalPlayer::ChangeInterior(char id)
{
    m_nCurrentInterior = id;
    if (m_pPed)
        m_pPed->LinkToInterior(id, TRUE);
}

void CLocalPlayer::Chat(const char*)
{}

void CLocalPlayer::EnterVehicle(int vehicle, BOOL passenger)
{
    if (vehicle < 0 || vehicle >= CVehiclePool::MAX_VEHICLES || !g_netGame)
        return;

    const ID id = static_cast<ID>(vehicle);
    auto* pool = g_netGame->GetVehiclePool();
    auto* target = pool ? pool->Get(id) : nullptr;
    if (!target || !target->HasNativeVehicle())
        return;

    if (g_netGame->EnterVehicle(id, passenger ? 1 : 0))
        m_nLastVehicle = id;
}

void CLocalPlayer::OnServerVehicleWarp(ID vehicle)
{
    m_nLastVehicle = vehicle;
    m_nCurrentVehicle = vehicle;
    m_damage.m_nVehicleUpdating = INVALID_ID;
}

void CLocalPlayer::ExitVehicle(int vehicle)
{
    if (vehicle < 0 || vehicle >= CVehiclePool::MAX_VEHICLES || !g_netGame)
        return;

    const ID id = static_cast<ID>(vehicle);
    auto* pool = g_netGame->GetVehiclePool();
    if (!pool || !pool->Get(id))
    {
        if (m_nLastVehicle == id)
            m_nLastVehicle = INVALID_ID;
        return;
    }

    if (g_netGame->ExitVehicle(id) && m_nLastVehicle == id)
    {
        m_nLastVehicle = INVALID_ID;
        m_damage.m_nVehicleUpdating = INVALID_ID;
    }
}

void CLocalPlayer::SendStats()
{}

void CLocalPlayer::UpdateVehicleDamage(ID vehicleId)
{
    auto* pool = g_netGame ? g_netGame->GetVehiclePool() : nullptr;
    auto* vehicle = pool ? pool->Get(vehicleId) : nullptr;
    if (!vehicle || !vehicle->HasNativeVehicle())
        return;

    const int panels = vehicle->GetPanelsDamage();
    const int doors = vehicle->GetDoorsDamage();
    const std::uint8_t lights = static_cast<std::uint8_t>(vehicle->GetLightsDamage());
    const std::uint8_t tires = static_cast<std::uint8_t>(vehicle->GetTires());

    if (m_damage.m_nVehicleUpdating != vehicleId)
    {
        m_damage.m_nVehicleUpdating = vehicleId;
        m_damage.m_nBumper = panels;
        m_damage.m_nDoor = doors;
        m_damage.m_bLight = lights;
        m_damage.m_bWheel = tires;
        return;
    }

    if (m_damage.m_nBumper == panels && m_damage.m_nDoor == doors &&
        m_damage.m_bLight == lights && m_damage.m_bWheel == tires)
        return;

    if (!g_netGame->VehicleDamaged(vehicleId, static_cast<std::uint32_t>(panels),
                                   static_cast<std::uint32_t>(doors), lights, tires))
        return;

    m_damage.m_nBumper = panels;
    m_damage.m_nDoor = doors;
    m_damage.m_bLight = lights;
    m_damage.m_bWheel = tires;
}

void CLocalPlayer::NextClass()
{
    if (!g_game || !g_netGame || !g_netGame->m_nClassesAvailable || !m_classSelection.m_bIsActive ||
        !m_bClearedToSpawn || m_classSelection.m_bWaitingForSpawnRequestReply || GetTickCount() - m_initialSelectionTick < 2000 ||
        GetTickCount() - m_lastSelectionTick <= 250)
        return;
    const int next = (m_classSelection.m_nSelected + 1) % static_cast<int>(g_netGame->m_nClassesAvailable);
    if (!g_netGame->RequestClass(static_cast<std::uint16_t>(next)))
        return;
    m_classSelection.m_nSelected = next;
    m_bClearedToSpawn = FALSE;
    m_lastSelectionTick = GetTickCount();
    CMatrix matrix = CMatrix::Unity();
    if (m_pPed)
        m_pPed->GetMatrix(&matrix);
    if (auto* audio = g_game->GetAudio())
        audio->Play(1052, matrix.GetPosition());
}

void CLocalPlayer::PrevClass()
{
    if (!g_game || !g_netGame || !g_netGame->m_nClassesAvailable || !m_classSelection.m_bIsActive ||
        !m_bClearedToSpawn || m_classSelection.m_bWaitingForSpawnRequestReply || GetTickCount() - m_initialSelectionTick < 2000 ||
        GetTickCount() - m_lastSelectionTick <= 250)
        return;
    const int previous = m_classSelection.m_nSelected <= 0 ? static_cast<int>(g_netGame->m_nClassesAvailable) - 1 : m_classSelection.m_nSelected - 1;
    if (!g_netGame->RequestClass(static_cast<std::uint16_t>(previous)))
        return;
    m_classSelection.m_nSelected = previous;
    m_bClearedToSpawn = FALSE;
    m_lastSelectionTick = GetTickCount();
    CMatrix matrix = CMatrix::Unity();
    if (m_pPed)
        m_pPed->GetMatrix(&matrix);
    if (auto* audio = g_game->GetAudio())
        audio->Play(1053, matrix.GetPosition());
}

void CLocalPlayer::ProcessClassSelection()
{
    if (!m_classSelection.m_bIsActive)
        return;
    if (GetTickCount() - m_lastSelectionTick > 1000 && m_classSelection.m_bWaitingForSpawnRequestReply)
        m_classSelection.m_bWaitingForSpawnRequestReply = FALSE;
    if (!g_game || CGame::IsPaused() || GetTickCount() - m_initialSelectionTick < 2000 ||
        (g_input && g_input->IsEnabled()) || (g_dialog && g_dialog->IsActive()))
        return;
    if (GetAsyncKeyState(VK_SHIFT) & 0x8000)
        RequestSpawn();
    else if (GetAsyncKeyState(VK_LEFT) & 0x8000)
        PrevClass();
    else if (GetAsyncKeyState(VK_RIGHT) & 0x8000)
        NextClass();
}

void CLocalPlayer::UpdateWeapons()
{
    // ABI placeholder only.
}

void CLocalPlayer::ProcessSpectating()
{
    auto* pools = g_netGame ? g_netGame->m_pPools : nullptr;
    auto* vehiclePool = pools ? pools->m_pVehicle : nullptr;

    if (!m_bDoesSpectating || !g_game)
        return;

    auto* camera = g_game->GetCamera();
    if (!camera)
        return;

    if (m_spectating.m_bProcessed)
        return;

    if (m_spectating.m_nType == SPEC_TYPE_VEHICLE && vehiclePool)
    {
        auto* vehicle = vehiclePool->Get(static_cast<ID>(m_spectating.m_nObject));
        if (vehicle && vehicle->HasNativeVehicle())
        {
            camera->TakeControl(*vehicle, m_spectating.m_nMode ? m_spectating.m_nMode : SPECTATING_MODE_VEHICLE, 2);
            m_spectating.m_bProcessed = TRUE;
        }
    }
    else if (m_spectating.m_nType == SPEC_TYPE_PLAYER)
    {
        auto* playerPool = g_netGame ? g_netGame->GetPlayerPool() : nullptr;
        auto* remote = playerPool ? playerPool->GetPlayer(static_cast<ID>(m_spectating.m_nObject)) : nullptr;
        auto* ped = remote ? remote->m_pPed : nullptr;
        if (ped && ped->HasNativePed())
        {
            camera->TakeControl(*ped, m_spectating.m_nMode ? m_spectating.m_nMode : SPECTATING_MODE_PLAYER, 2);
            m_spectating.m_bProcessed = TRUE;
        }
    }
    else if (m_spectating.m_nType == SPEC_TYPE_NONE)
    {
        camera->Set(CVector{50.0f, 50.0f, 50.0f}, CVector{});
        camera->PointAt(CVector{60.0f, 60.0f, 50.0f}, 2);

        m_spectating.m_bProcessed = TRUE;
    }
}

void CLocalPlayer::SendTakeDamage(int, float, int, int)
{
    // ABI placeholder only.
}

void CLocalPlayer::SendGiveDamage(int, float, int, int)
{
    // ABI placeholder only.
}

bool CLocalPlayer::ProcessUnoccupiedSync(ID vehicleId, CVehicle* vehicle)
{
    if (!vehicle || vehicleId >= CVehiclePool::MAX_VEHICLES || !vehicle->HasNativeVehicle() ||
        !m_pPed || !m_pPed->HasNativePed() || m_pPed->IsDead() || !m_bIsActive || m_bDoesSpectating ||
        !g_netGame || g_netGame->GetState() != CNetGame::GAME_MODE_CONNECTED || vehicle->HasPlayerDriver())
        return false;

    const float distance = vehicle->GetDistanceToLocalPlayer();
    if (vehicle->IsTrainPart() || !std::isfinite(distance) || distance >= 90.0f ||
        vehicle->IsStationary() || vehicle->GetTractor())
    {
        vehicle->m_bUnoccupiedSync = FALSE;
        vehicle->m_bRemoteUnocSync = FALSE;
        return false;
    }

    const int seat = vehicle->GetUnoccupiedSyncSeat(m_pPed->GetNativePedHandle());
    if (seat < 0)
        return false;
    if (seat > 0)
    {
        vehicle->m_bUnoccupiedSync = FALSE;
        SendUnoccupiedData(vehicleId, static_cast<char>(seat));
        return vehicle->m_bUnoccupiedSync != FALSE;
    }

    auto* playerPool = g_netGame->GetPlayerPool();
    if (!playerPool)
        return false;

    const ID localId = playerPool->m_localInfo.m_nId;
    const int lastId = std::min(CPlayerPool::MAX_PLAYERS - 1,
        std::max(static_cast<int>(localId), playerPool->m_nLargestId));
    float nearestDistance = 100000.0f;
    ID nearestId = CPlayerPool::INVALID_ID;

    for (int id = 0; id <= lastId; ++id)
    {
        auto* remote = id == localId ? nullptr : playerPool->GetPlayer(static_cast<CPlayerPool::ID>(id));
        auto* ped = id == localId ? m_pPed : remote ? remote->m_pPed : nullptr;
        if (!ped || !ped->HasNativePed() || !ped->IsValid())
            continue;

        const float candidateDistance = ped->GetDistanceToEntity(vehicle);
        if (std::isfinite(candidateDistance) && candidateDistance < nearestDistance)
        {
            nearestDistance = candidateDistance;
            nearestId = static_cast<ID>(id);
        }
    }

    if (nearestId != localId || nearestDistance >= 90.0f)
        return false;

    vehicle->m_bUnoccupiedSync = FALSE;
    SendUnoccupiedData(vehicleId, 0);
    return vehicle->m_bUnoccupiedSync != FALSE;
}

void CLocalPlayer::EnterVehicleAsPassenger()
{
    auto* pools = g_netGame ? g_netGame->m_pPools : nullptr;
    auto* vehiclePool = pools ? pools->m_pVehicle : nullptr;

    auto* menuPool = pools ? pools->m_pMenu : nullptr;
    if (!m_pPed || !m_pPed->HasNativePed() || m_pPed->IsDead() || m_pPed->GetVehicle() || !vehiclePool || m_bDoesSpectating || m_classSelection.m_bIsActive ||
        !(m_bIsActive || g_settings.m_bDebugMode) || game::inputhandler::game_input_blocked() || CGame::IsPaused() ||
        (menuPool && menuPool->m_nCurrent != CMenuPool::INVALID_MENU) || (!g_game || GetForegroundWindow() != static_cast<HWND>(g_game->GetWindowHandle())))
        return;
    const ID id = vehiclePool->GetNearest();
    auto* vehicle = id != CVehiclePool::INVALID_ID ? vehiclePool->Get(id) : nullptr;
    if (!vehicle || !vehicle->HasNativeVehicle() || !vehicle->m_handle || !(m_pPed->GetDistanceToEntity(vehicle) < 4.5f))
        return;

    // Опкод 0x0470 (get_current_char_weapon): отримує ID поточної зброї персонажа
    int weapon = 0;
    constexpr scm::OpcodeInfo opGetWeapon{0x0470, "iv"};
    scm::ProcessCommand(&opGetWeapon, m_pPed->m_handle, &weapon);
    if (weapon == 46)
    {
        // Опкод 0x01B9 (set_char_current_weapon): примусово змінює поточну зброю персонажа (в даному випадку скидає парашут/зброю на 0 - кулаки)
        constexpr scm::OpcodeInfo opSetWeapon{0x01B9, "ii"};
        scm::ProcessCommand(&opSetWeapon, m_pPed->m_handle, 0);
    }
    m_pPed->EnterVehicle(vehicle->m_handle, TRUE);
    EnterVehicle(id, TRUE);
}

void CLocalPlayer::ProcessPassengerEntry(const CPad& keys)
{
    if (!keys.DisablePlayerControls && !keys.bDisablePlayerEnterCar && keys.NewState.DPadUp && !keys.OldState.DPadUp)
        EnterVehicleAsPassenger();
}

void CLocalPlayer::SendIncarData()
{
    if (!m_pPed || !m_pPed->HasNativePed() || !g_netGame)
        return;

    auto* vehiclePool = g_netGame->GetVehiclePool();
    const ID vehicleId = vehiclePool ? vehiclePool->Find(m_pPed->GetVehicle()) : INVALID_ID;
    auto* vehicle = vehicleId != INVALID_ID ? vehiclePool->Get(vehicleId) : nullptr;
    if (!vehicle || !vehicle->HasNativeVehicle())
        return;

    Synchronization::IncarData data{};
    data.m_nVehicle = vehicleId;
    short leftX = 0, leftY = 0;
    data.m_controllerState.m_value = m_pPed->GetKeys(&leftX, &leftY);
    data.m_controllerState.m_sLeftStickX = leftX;
    data.m_controllerState.m_sLeftStickY = leftY;

    CMatrix matrix = CMatrix::Unity();
    vehicle->GetMatrix(&matrix);
    SaveRotation(matrix, data.m_fQuaternion);
    data.m_position = matrix.GetPosition();
    vehicle->GetSpeed(&data.m_speed);
    data.m_fHealth = vehicle->GetHealth();
    data.m_nDriverHealth = HealthByte(m_pPed->GetHealth());
    data.m_nDriverArmor = HealthByte(m_pPed->GetArmour());

    CPad pad{};
    std::uint8_t extraKeys = 0;
    if (const auto* internalKeys = KeyStuff::GetInternalKeys())
    {
        std::memcpy(&pad, internalKeys, sizeof(pad));
        extraKeys = GetExtraKeyState(pad);
    }
    data.m_nCurrentWeapon = Synchronization::PackWeaponAndKeys(m_pPed->GetCurrentWeapon(), extraKeys);
    data.m_bSirenEnabled = vehicle->SirenEnabled() != FALSE;
    data.m_bLandingGear = vehicle->GetLandingGearState() != FALSE;

    if (auto* trailer = vehicle->GetTrailer())
    {
        const ID trailerId = vehiclePool->Find(trailer->GetNativeVehicleHandle());
        if (trailerId != INVALID_ID)
            data.m_nTrailerId = trailerId;
    }

    const int model = vehicle->GetModelIndex();
    if (model == 537 || model == 538 || model == 449)
        data.m_fTrainSpeed = vehicle->GetTrainSpeed();
    else if (model == 520)
    {
        const auto thrust = static_cast<std::uint32_t>(vehicle->GetHydraThrusters());
        data.m_aHydraThrustAngle[0] = static_cast<std::uint16_t>(thrust);
        data.m_aHydraThrustAngle[1] = static_cast<std::uint16_t>(thrust >> 16);
    }

    if (!FiniteVector(data.m_position) || !FiniteVector(data.m_speed) || !FiniteRotation(data.m_fQuaternion) || !std::isfinite(data.m_fHealth) ||
        ((model == 537 || model == 538 || model == 449) && !std::isfinite(data.m_fTrainSpeed)))
        return;

    vehicle->m_bUnoccupiedSync = FALSE;
    vehicle->m_bRemoteUnocSync = FALSE;
    vehiclePool->m_nLastUndrivenId[vehicleId] = INVALID_ID;
    if (!SendSyncPacket(ID_VEHICLE_SYNC, data))
        return;

    m_incarData = data;
    m_nCurrentVehicle = vehicleId;
    if (data.m_nTrailerId != 0)
        SendTrailerData(data.m_nTrailerId);
}

void CLocalPlayer::Process()
{
    if (auto* game = g_game.get())
        m_pPed = game->GetPlayerPed();
    if (!m_pPed || !m_pPed->HasNativePed())
        return;

    CPad snapshot{};
    if (const auto* internalKeys = KeyStuff::GetInternalKeys())
    {
        std::memcpy(&snapshot, internalKeys, sizeof(snapshot));
        ProcessPassengerEntry(snapshot);
    }

    const TICK now = GetTickCount();
    if (m_bDoesSpectating)
    {
        ProcessSpectating();
        m_bPassengerDriveBy = FALSE;
        return;
    }

    if (!m_bIsActive)
    {
        ProcessClassSelection();
        return;
    }

    if (m_pPed->IsDead())
    {
        if (!m_bIsWasted)
        {
            WastedNotification();
            m_bIsActive = FALSE;
            m_bIsWasted = TRUE;
        }
        return;
    }

    const void* currentNativeVehicle = m_pPed->GetVehicle();
    auto* vehiclePool = g_netGame ? g_netGame->GetVehiclePool() : nullptr;
    const ID currentVehicleId = currentNativeVehicle && vehiclePool ? vehiclePool->Find(const_cast<void*>(currentNativeVehicle)) : INVALID_ID;
    if (m_nLastVehicle != INVALID_ID && m_nLastVehicle != currentVehicleId &&
        (currentVehicleId != INVALID_ID || m_nCurrentVehicle == m_nLastVehicle))
        ExitVehicle(m_nLastVehicle);
    if (currentVehicleId != INVALID_ID && m_nLastVehicle != currentVehicleId)
        EnterVehicle(currentVehicleId, m_pPed->IsPassenger());

    if (m_pPed->GetVehicle())
    {
        if (m_pPed->IsPassenger())
        {
            if (now - m_lastUpdate > static_cast<TICK>(GetIncarSendRate()))
            {
                m_lastUpdate = now;
                SendPassengerData();
            }
        }
        else
        {
            ProcessIncarWorldBounds();
            if (g_netGame)
            {
                auto* pool = g_netGame->GetVehiclePool();
                const ID id = pool ? pool->Find(m_pPed->GetVehicle()) : INVALID_ID;
                if (id != INVALID_ID)
                    UpdateVehicleDamage(id);
            }
            if (now - m_lastUpdate > static_cast<TICK>(GetIncarSendRate()))
            {
                m_lastUpdate = now;
                SendIncarData();
            }
        }
        m_bPassengerDriveBy = FALSE;
    }
    else
    {
        ProcessOnfootWorldBounds();
        ProcessSurfing();
        ProcessHead();

        short leftStickX = 0, leftStickY = 0;
        const short controllerState = m_pPed->GetKeys(&leftStickX, &leftStickY);
        if (NeedsToSendOnfootData(controllerState, leftStickY, leftStickX) || now - m_lastUpdate > static_cast<TICK>(GetOnfootSendRate()))
        {
            m_lastUpdate = now;
            SendOnfootData();
        }
    }

    if (now - m_lastAimUpdate >= CNetGame::NETMODE_AIM_UPDATE_DELAY)
        SendAimData();

    if (now - m_lastStatsUpdate > STATS_UPDATE_MS)
    {
        m_lastStatsUpdate = now;
        SendStats();
    }
    UpdateCameraTarget();
}
