#include <stdafx.h>
#include "game/Profiler.h"
#include "main.h"
#include "net/CNetGame.h"
#include "players/PlayerColors.h"

#include "CLocalPlayer.h"
#include "CRemotePlayer.h"

#include "game/CCamera.h"
#include "sync/BulletSync.h"
#include "game/CAudio.h"
#include "gui/CDialog.h"
#include "game/CGame.h"
#include "game/InputHandler.h"
#include "gui/CInput.h"
#include "config/CSettings.h"
#include "game/CPad.h"
#include "game/PlayerSlots.h"
#include "game/Scripting.h"
#include "sync/KeyStuff.h"
#include "sync/AimStuff.h"
#include "sync/SpecialAction.h"
#include "game/entities/CObject.h"
#include "game/entities/CPed.h"
#include "game/entities/CVehicle.h"
#include "render/render.h"
#include "pools/CObjectPool.h"
#include "pools/CActorPool.h"
#include "gui/CLabel.h"
#include "pools/CVehiclePool.h"
#include "pools/CMenuPool.h"
#include "sync/Math.inl"

#include <BitStream.h>
#include <PacketEnumerations.h>
#include <RakClient.h>

#include <cmath>
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

    struct SelectionKeyState
    {
        const CLocalPlayer* player = nullptr;
        bool active = false;
        bool shift = false;
        bool left = false;
        bool right = false;
    };

    SelectionKeyState g_selectionKeys{};

    bool IsKeyHeld(int key)
    {
        return (GetAsyncKeyState(key) & 0x8000) != 0;
    }

    void CaptureSelectionKeys(const CLocalPlayer* player, bool active)
    {
        g_selectionKeys = {player, active, IsKeyHeld(VK_SHIFT), IsKeyHeld(VK_LEFT), IsKeyHeld(VK_RIGHT)};
    }

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
        sync::math::QuaternionToWire(sync::math::QuaternionFromMatrix(matrix), quaternion);
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
    m_incarData = {};
    m_aimData = {};
    m_trailerData = {};
    m_onfootData = {};
    m_passengerData = {};
    m_bIsActive = FALSE;
    m_bIsWasted = FALSE;
    m_nCurrentVehicle = INVALID_ID;
    m_nLastVehicle = INVALID_ID;
    m_animation = {};
    field_100 = 0;
    m_pPed = nullptr;
    m_bDoesSpectating = FALSE;
    m_nTeam = 0xFF;
    field_10d = 0;
    m_cameraTarget = {};
    m_head = {};
    m_bClearedToSpawn = FALSE;
    m_spawnInfo = {};
    m_bHasSpawnInfo = FALSE;
    m_weaponsData = {};
    m_bPassengerDriveBy = FALSE;
    m_nCurrentInterior = 0;
    m_bInRCMode = FALSE;
    std::memset(m_szName, 0, sizeof(m_szName));
    m_surfing = {};
    m_classSelection = {};
    m_spectating = {};
    m_damage = {};

    const TICK now = GetTickCount();
    m_lastSelectionTick = now;
    m_initialSelectionTick = 0;
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
    if (g_selectionKeys.player == this)
        g_selectionKeys = {};
    m_nCurrentInterior = 0;
    m_nLastVehicle = INVALID_ID;
    m_nCurrentVehicle = INVALID_ID;
    m_vehicleEntryPending = false;
    m_vehicleExitPending = false;
    m_vehicleTransitionStarted = 0;
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
    const ID invalidId = INVALID_ID;
    std::memcpy(m_weaponsData.data, &invalidId, sizeof(invalidId));
    std::memcpy(m_weaponsData.data + sizeof(invalidId), &invalidId, sizeof(invalidId));

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
            m_pPed->StopJetpack();
            if (m_pPed->IsHoldingStuff())
                m_pPed->DropStuff();
            if (m_pPed->DoesDancing())
                m_pPed->StopDancing();
            if (m_pPed->UsingCellphone())
                m_pPed->EnableCellphone(FALSE);
            if (m_pPed->DoesUrinating())
                m_pPed->StopUrinating();
            break;
        case SPECIAL_ACTION_USEJETPACK:
            m_pPed->StartJetpack();
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
        case SPECIAL_ACTION_DRINK_BEER:
        case SPECIAL_ACTION_DRINK_WINE:
        case SPECIAL_ACTION_DRINK_SPRUNK:
        case SPECIAL_ACTION_SMOKE_CIGGY:
        {
            const auto action = static_cast<unsigned char>(id);
            const int type = action == SPECIAL_ACTION_DRINK_BEER ? 1 : action == SPECIAL_ACTION_DRINK_WINE ? 2 : action == SPECIAL_ACTION_DRINK_SPRUNK ? 3 : 4;
            if (m_pPed->GetStuff() != type)
                m_pPed->GiveStuff(type);
            break;
        }
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
    if (m_pPed->HasVehicleEntryTask())
        return SPECIAL_ACTION_ENTER_VEHICLE;
    if (m_pPed->HasVehicleExitTask())
        return SPECIAL_ACTION_EXIT_VEHICLE;
    if (m_pPed->IsInJetpackMode())
        return SPECIAL_ACTION_USEJETPACK;
    if (m_pPed->DoesDancing())
        return static_cast<char>(SPECIAL_ACTION_DANCE1 + std::clamp(m_pPed->m_nDanceStyle, 0u, 3u));
    if (m_pPed->DoesHandsUp())
        return SPECIAL_ACTION_HANDSUP;
    if (m_pPed->UsingCellphone())
        return SPECIAL_ACTION_USECELLPHONE;
    if (m_pPed->DoesUrinating())
        return SPECIAL_ACTION_URINATING;
    switch (m_pPed->GetStuff())
    {
        case 1:
            return SPECIAL_ACTION_DRINK_BEER;
        case 2:
            return SPECIAL_ACTION_DRINK_WINE;
        case 3:
            return SPECIAL_ACTION_DRINK_SPRUNK;
        case 4:
            return SPECIAL_ACTION_SMOKE_CIGGY;
        default:
            break;
    }
    return SPECIAL_ACTION_NONE;
}

void CLocalPlayer::UpdateSurfing()
{
    if (!m_pPed || !m_pPed->HasNativePed() || !m_surfing.m_pVehicle)
        return;

    auto* entity = reinterpret_cast<CEntity*>(m_surfing.m_pVehicle);
    if (!entity->IsValid())
        return;

    CMatrix matrix = CMatrix::Unity();
    entity->GetMatrix(&matrix);
    const CVector offset = matrix.InverseTransformPoint(PositionOf(m_pPed));
    if (!FiniteVector(offset))
        return;

    m_surfing.m_position = offset;
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
    return RefIncarSendrate() + game::players::GetActiveSlotCount();
}

int CLocalPlayer::GetOnfootSendRate()
{
    if (!m_pPed)
        return 1000;
    if (g_netGame && g_netGame->LanMode())
        return 15;
    return RefOnfootSendrate() + game::players::GetActiveSlotCount();
}

int CLocalPlayer::GetUnoccupiedSendRate()
{
    if (!m_pPed)
        return 1000;
    return (m_pPed->GetFlags() & 0x100) ? GetIncarSendRate() : GetOnfootSendRate();
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

    // server-forced spawning can arrive before gta finishes resurrection
    if (ped->IsDead())
    {
        if (!ped->RestoreAfterDeath(position, rotation))
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

    ped->SetDrunkLevel(0);
    m_bIsWasted = FALSE;
    m_bIsActive = TRUE;
    m_classSelection.m_bIsActive = FALSE;
    if (g_selectionKeys.player == this)
        g_selectionKeys = {};
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
    CaptureSelectionKeys(this, true);
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

    if (!enable && m_classSelection.m_bEnableAfterDeath)
    {
        m_bIsActive = FALSE;
        m_bHasSpawnInfo = FALSE;
    }

    m_bDoesSpectating = enable;
    if (enable)
    {
        m_lastSpecUpdate = 0;
        m_lastAimUpdate = 0;
    }
    m_spectating.m_nMode = 0;
    m_spectating.m_nType = SPEC_TYPE_NONE;
    m_spectating.m_nObject = -1;
    m_spectating.m_bProcessed = FALSE;

    if (!enable)
    {
        if (!m_bIsActive && g_netGame)
        {
            m_classSelection.m_bEnableAfterDeath = FALSE;
            g_netGame->SpawnScreen();
            RequestClass(m_classSelection.m_nSelected);
        }
        return;
    }

    render::ShowClassSelection(false);

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
    if (!g_netGame || !g_netGame->m_pPools)
        return false;
    const auto* aim = AimStuff::GetAim();
    if (!aim || !FiniteVector(aim->source) || !FiniteVector(aim->front))
        return false;
    const CVector origin = aim->source + aim->front;
    const CVector end = aim->source + aim->front * 70.0f;
    auto find = [&](auto* pool, int count, float range, float scale, const CVector& target, auto get) -> ID {
        ID result = INVALID_ID;
        float nearest = 100000.0f;
        if (!pool)
            return result;
        for (int id = 0; id < count; ++id)
        {
            auto* entity = get(pool, static_cast<ID>(id));
            if (!entity || !entity->IsValid())
                continue;
            const float distance = entity->GetDistanceToPoint(origin);
            if (entity->GetDistanceToCamera() < range && distance < nearest && entity->IntersectsCameraRay(origin, target, scale))
            {
                nearest = distance;
                result = static_cast<ID>(id);
            }
        }
        return result;
    };
    auto* pools = g_netGame->m_pPools;
    CVector target = CCamera::TraceTarget(origin, end, false, false);
    info->m_nObject = find(pools->m_pObject, CObjectPool::MAX_OBJECTS, 40.0f, 1.0f, target, [](auto* pool, ID id) {
        auto* object = pool->Get(id);
        return object && object->GetModelIndex() != 19300 ? object : nullptr;
    });
    target = CCamera::TraceTarget(origin, end, false, true);
    info->m_nVehicle = find(pools->m_pVehicle, CVehiclePool::MAX_VEHICLES, 40.0f, 1.0f, target, [](auto* pool, ID id) {
        return pool->Get(id);
    });
    target = CCamera::TraceTarget(origin, end, true, true);
    info->m_nPlayer = find(pools->m_pPlayer, CPlayerPool::MAX_PLAYERS, 20.0f, 1.5f, target, [](auto* pool, ID id) {
        auto* player = pool->GetPlayer(id);
        return player ? player->m_pPed : nullptr;
    });
    info->m_nActor = find(pools->m_pActor, CActorPool::MAX_ACTORS, 20.0f, 1.5f, target, [](auto* pool, ID id) {
        return pool->Get(id);
    });
    return true;
}

void CLocalPlayer::UpdateCameraTarget()
{
    if (!g_netGame || !g_netGame->m_bUpdateCameraTarget)
    {
        ResetCameraTarget(m_cameraTarget);
        return;
    }
    const TICK now = GetTickCount();
    if (now - m_lastCameraTargetUpdate <= CAMERA_TARGET_UPDATE_MS)
        return;

    CameraTarget target{};
    ResetCameraTarget(target);
    DefineCameraTarget(&target);
    if (std::memcmp(&target, &m_cameraTarget, sizeof(target)) != 0)
    {
        if (g_netGame->CameraTarget(target.m_nObject, target.m_nVehicle, target.m_nPlayer, target.m_nActor))
            m_cameraTarget = target;
    }
    m_lastCameraTargetUpdate = now;
}

void CLocalPlayer::DrawCameraTargetLabel()
{
    if (!g_label || !g_netGame || !g_netGame->m_pPools)
        return;
    auto draw = [](CEntity* entity, ID id, const char* type) {
        if (id == INVALID_ID || !entity || !entity->IsValid())
            return;
        CVector position = PositionOf(entity);
        char text[80];
        std::snprintf(text, sizeof(text), "CAMERA_TARGET_TYPE_%s: ID(%u)", type, static_cast<unsigned>(id));
        g_label->Draw(&position, text, 0xFFD47B75, TRUE, false);
    };
    auto* pools = g_netGame->m_pPools;
    g_label->Begin();
    draw(pools->m_pObject ? pools->m_pObject->Get(m_cameraTarget.m_nObject) : nullptr, m_cameraTarget.m_nObject, "OBJECT");
    draw(pools->m_pVehicle ? pools->m_pVehicle->Get(m_cameraTarget.m_nVehicle) : nullptr, m_cameraTarget.m_nVehicle, "VEHICLE");
    auto* player = pools->m_pPlayer ? pools->m_pPlayer->GetPlayer(m_cameraTarget.m_nPlayer) : nullptr;
    draw(player ? player->m_pPed : nullptr, m_cameraTarget.m_nPlayer, "PLAYER");
    draw(pools->m_pActor ? pools->m_pActor->Get(m_cameraTarget.m_nActor) : nullptr, m_cameraTarget.m_nActor, "ACTOR");
    g_label->End();
}

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

    if (!FiniteVector(data.m_roll) || !FiniteVector(data.m_direction) || !FiniteVector(data.m_position) || !FiniteVector(data.m_speed) || !FiniteVector(data.m_turnSpeed) ||
        !std::isfinite(data.m_fHealth))
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
    m_animation = m_pPed->GetScriptAnimation();
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
    data.m_nWeaponState = m_pPed->GetWeaponState();

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
{
    if (!g_netGame || !m_pPed || !m_pPed->HasNativePed())
        return;

    std::uint8_t reason = 0xFF;
    ID responsible = INVALID_ID;
    void* source{};
    reason = m_pPed->GetDeathReason(source);
    {
        const bool directSource = reason < 0x2B || reason == 0x32 || reason == 0x33;
        const bool vehicleSource = reason == 0x31 || reason == 0x36;
        const bool knownSource = source && g_netGame->GetPlayerPool();
        bool recognized = reason == 0x35;
        if (knownSource && (directSource || vehicleSource))
        {
            auto* players = g_netGame->GetPlayerPool();
            if (directSource)
                responsible = players->Find(source);
            if (responsible != INVALID_ID)
                recognized = true;
            else
            {
                auto* vehicles = g_netGame->GetVehiclePool();
                const auto vehicleId = vehicles ? vehicles->Find(source) : CVehiclePool::INVALID_ID;
                auto* vehicle = vehicleId != CVehiclePool::INVALID_ID ? vehicles->Get(vehicleId) : nullptr;
                if (vehicle && vehicle->HasNativeVehicle() && vehicle->GetNativeVehicleHandle() == source)
                {
                    responsible = players->Find(vehicle->GetDriver());
                    recognized = responsible != INVALID_ID;
                }
                else if (reason == 0x36)
                    recognized = true;
            }
        }
        if (!recognized)
            reason = 0xFF;
    }

    g_netGame->DeathNotification(reason, responsible);
}

void CLocalPlayer::RequestClass(int id)
{
    if (g_netGame)
        g_netGame->RequestClass(id);
}

void CLocalPlayer::ChangeInterior(char id)
{
    if (g_netGame && g_netGame->InteriorChangeNotification(static_cast<std::uint8_t>(id)))
        m_nCurrentInterior = id;
}

void CLocalPlayer::Chat(const char* text)
{
    if (text && g_netGame)
        g_netGame->ChatMessage(text);
}

void CLocalPlayer::EnterVehicle(int vehicle, BOOL passenger)
{
    if (vehicle < 0 || vehicle >= CVehiclePool::MAX_VEHICLES || !g_netGame)
        return;

    const ID id = static_cast<ID>(vehicle);
    auto* pool = g_netGame->GetVehiclePool();
    auto* target = pool ? pool->Get(id) : nullptr;
    if (!target || !target->HasNativeVehicle())
        return;

    if (m_nLastVehicle == id || m_vehicleExitPending || !m_bIsActive)
        return;
    if (g_netGame->EnterVehicle(id, passenger ? 1 : 0))
    {
        m_nLastVehicle = id;
        m_vehicleEntryPending = true;
        m_vehicleTransitionStarted = g_game ? static_cast<TICK>(g_game->GetTimer()) : GetTickCount();
#if defined(_DEBUG)
        Log("[sync:enter] vehicle=%u passenger=%d", static_cast<unsigned>(id), passenger != FALSE);
#endif
    }
}

void CLocalPlayer::OnServerVehicleWarp(ID vehicle)
{
    m_nLastVehicle = vehicle;
    m_nCurrentVehicle = vehicle;
    m_vehicleEntryPending = false;
    m_vehicleExitPending = false;
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
        {
            m_nLastVehicle = INVALID_ID;
            m_vehicleEntryPending = m_vehicleExitPending = false;
        }
        return;
    }

    if (m_nLastVehicle != id || m_vehicleExitPending)
        return;
    if (g_netGame->ExitVehicle(id))
    {
        m_nLastVehicle = INVALID_ID;
        m_vehicleEntryPending = false;
        m_vehicleTransitionStarted = g_game ? static_cast<TICK>(g_game->GetTimer()) : GetTickCount();
        m_vehicleExitPending = m_pPed && (m_pPed->IsInVehicle() || m_pPed->HasVehicleExitTask());
        m_damage.m_nVehicleUpdating = INVALID_ID;
    }
}

void CLocalPlayer::SendStats()
{
    if (!g_game || !g_netGame || !m_pPed || !m_pPed->HasNativePed() || g_netGame->GetState() != CNetGame::GAME_MODE_CONNECTED)
        return;
    auto* client = g_netGame->GetRakClient();
    if (!client)
        return;

    RakNet::BitStream bs;
    bs.Write(static_cast<std::uint8_t>(ID_STATS_UPDATE));
    bs.Write(static_cast<std::int32_t>(g_game->GetPlayerMoney()));
    bs.Write(static_cast<std::int32_t>(m_pPed->GetDrunkLevel()));
    client->Send(&bs, HIGH_PRIORITY, UNRELIABLE, 0);
}

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

    if (m_damage.m_nBumper == panels && m_damage.m_nDoor == doors && m_damage.m_bLight == lights && m_damage.m_bWheel == tires)
        return;

    if (!g_netGame->VehicleDamaged(vehicleId, static_cast<std::uint32_t>(panels), static_cast<std::uint32_t>(doors), lights, tires))
        return;

    m_damage.m_nBumper = panels;
    m_damage.m_nDoor = doors;
    m_damage.m_bLight = lights;
    m_damage.m_bWheel = tires;
}

void CLocalPlayer::NextClass()
{
    if (!g_game || !g_netGame || !g_netGame->m_nClassesAvailable || !m_classSelection.m_bIsActive || !m_bClearedToSpawn || m_classSelection.m_bWaitingForSpawnRequestReply)
        return;
    const int next = (m_classSelection.m_nSelected + 1) % static_cast<int>(g_netGame->m_nClassesAvailable);
    if (!g_netGame->RequestClass(next))
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
    if (!g_game || !g_netGame || !g_netGame->m_nClassesAvailable || !m_classSelection.m_bIsActive || !m_bClearedToSpawn || m_classSelection.m_bWaitingForSpawnRequestReply)
        return;
    const int previous = m_classSelection.m_nSelected <= 0 ? static_cast<int>(g_netGame->m_nClassesAvailable) - 1 : m_classSelection.m_nSelected - 1;
    if (!g_netGame->RequestClass(previous))
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
    const bool active = m_classSelection.m_bIsActive != FALSE;
    if (g_selectionKeys.player != this || g_selectionKeys.active != active)
    {
        CaptureSelectionKeys(this, active);
        return;
    }

    const bool shift = IsKeyHeld(VK_SHIFT);
    const bool left = IsKeyHeld(VK_LEFT);
    const bool right = IsKeyHeld(VK_RIGHT);

    const bool shiftPressed = shift && !g_selectionKeys.shift;
    const bool leftPressed = left && !g_selectionKeys.left;
    const bool rightPressed = right && !g_selectionKeys.right;

    g_selectionKeys.shift = shift;
    g_selectionKeys.left = left;
    g_selectionKeys.right = right;

    if (!active)
        return;

    if (GetTickCount() - m_lastSelectionTick > 1000 && m_classSelection.m_bWaitingForSpawnRequestReply)
        m_classSelection.m_bWaitingForSpawnRequestReply = FALSE;

    if (!g_game || CGame::IsPaused() || (g_input && g_input->IsEnabled()) || (g_dialog && g_dialog->IsActive()))
        return;

    if (shiftPressed)
        RequestSpawn();
    else if (leftPressed)
        PrevClass();
    else if (rightPressed)
        NextClass();
}

void CLocalPlayer::SendBulletData(CVector origin, CVector target, void* entity)
{
    if (!m_bIsActive || m_bIsWasted || m_bDoesSpectating || !m_pPed || !m_pPed->HasNativePed() || !FiniteVector(origin) || !FiniteVector(target))
        return;
    const auto weapon = m_pPed->GetCurrentWeapon();
    if (weapon < 22 || weapon > 34 || !g_netGame)
        return;
    auto* pools = g_netGame->m_pPools;
    if (!pools || (weapon == 34 && !entity))
        return;
    Synchronization::BulletData data{};
    data.m_nTargetId = INVALID_ID;
    data.m_origin = origin;
    data.m_target = target;
    data.m_nWeapon = weapon;
    if (entity)
    {
        auto id = pools->m_pPlayer ? pools->m_pPlayer->Find(entity) : INVALID_ID;
        if (id != INVALID_ID)
        {
            data.m_nTargetType = 1;
            data.m_nTargetId = id;
        }
        else if ((id = pools->m_pVehicle ? pools->m_pVehicle->Find(entity) : INVALID_ID) != INVALID_ID)
        {
            data.m_nTargetType = 2;
            data.m_nTargetId = id;
        }
        else
        {
            const auto object = pools->m_pObject ? pools->m_pObject->GetId(entity) : -1;
            if (object >= 0 && object < CObjectPool::MAX_OBJECTS)
            {
                data.m_nTargetType = 3;
                data.m_nTargetId = static_cast<ID>(object);
            }
        }
        data.m_center = game::bullets::HitOffset(entity, target, CNetGame::RefLagCompMode());
    }
    if (FiniteVector(data.m_center))
        SendSyncPacket(ID_BULLET_SYNC, data);
}

void CLocalPlayer::UpdateWeapons()
{
    if (!g_netGame || !m_pPed || !m_pPed->HasNativePed() || g_netGame->GetState() != CNetGame::GAME_MODE_CONNECTED)
        return;
    const auto* pools = g_netGame->m_pPools;
    void* target = m_pPed->GetTargetEntity();
    const ID playerId = target && pools && pools->m_pPlayer ? pools->m_pPlayer->Find(target) : INVALID_ID;
    const ID actorId = target && pools && pools->m_pActor ? pools->m_pActor->Find(target) : INVALID_ID;
    ID oldPlayer{}, oldActor{};
    std::memcpy(&oldPlayer, m_weaponsData.data, sizeof(oldPlayer));
    std::memcpy(&oldActor, m_weaponsData.data + 2, sizeof(oldActor));
    const auto slot = m_pPed->GetWeaponSlot();
    const TICK now = GetTickCount();
    if (playerId == oldPlayer && actorId == oldActor && slot == m_weaponsData.data[4] && now - m_lastWeaponsUpdate < 100)
        return;

    RakNet::BitStream bs;
    bs.Write(static_cast<std::uint8_t>(ID_WEAPONS_UPDATE));
    bs.Write(playerId);
    bs.Write(actorId);
    auto next = m_weaponsData;
    std::memcpy(next.data, &playerId, sizeof(playerId));
    std::memcpy(next.data + 2, &actorId, sizeof(actorId));
    next.data[4] = slot;
    m_weaponsData.data[4] = slot;
    bool changed = playerId != oldPlayer || actorId != oldActor;
    for (std::uint8_t index = 0; index < 13; ++index)
    {
        int weapon{};
        std::uint32_t ammo{}, previousAmmo{};
        if (!m_pPed->GetWeaponData(index, weapon, ammo))
            return;
        std::memcpy(&previousAmmo, m_weaponsData.data + 18 + index * 4, sizeof(previousAmmo));
        if (weapon == m_weaponsData.data[5 + index] && ammo == previousAmmo)
            continue;
        next.data[5 + index] = static_cast<std::uint8_t>(weapon);
        std::memcpy(next.data + 18 + index * 4, &ammo, sizeof(ammo));
        bs.Write(index);
        bs.Write(static_cast<std::uint8_t>(weapon));
        bs.Write(static_cast<std::uint16_t>(ammo));
        changed = true;
    }
    auto* client = g_netGame->GetRakClient();
    if (changed && client && client->Send(&bs, HIGH_PRIORITY, UNRELIABLE, 0))
    {
        m_weaponsData = next;
        m_lastWeaponsUpdate = now;
    }
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

    // spectator sync uses camera position, not the hidden player's position.
    if (m_pPed && m_pPed->HasNativePed())
    {
        const TICK now = GetTickCount();
        const auto rate = static_cast<TICK>(std::max(1, GetOnfootSendRate()));
        if (now - m_lastSpecUpdate > rate)
        {
            Synchronization::SpectatorData data{};
            short leftStickX{}, leftStickY{};
            data.m_controllerState.m_value = m_pPed->GetKeys(&leftStickX, &leftStickY);
            data.m_controllerState.m_sLeftStickX = leftStickX;
            data.m_controllerState.m_sLeftStickY = leftStickY;

            CMatrix matrix = CMatrix::Unity();
            camera->GetMatrix(&matrix);
            data.m_position = matrix.GetPosition();
            if (FiniteVector(data.m_position) && SendSyncPacket(ID_SPECTATOR_SYNC, data))
                m_lastSpecUpdate = now;
        }
        if (now - m_lastAimUpdate > rate * 2)
            SendAimData();
    }

    if (m_spectating.m_nType == SPEC_TYPE_PLAYER)
    {
        auto* playerPool = g_netGame ? g_netGame->GetPlayerPool() : nullptr;
        auto* remote = playerPool ? playerPool->GetPlayer(static_cast<ID>(m_spectating.m_nObject)) : nullptr;
        if (!remote || !remote->m_pPed || !remote->m_pPed->HasNativePed() || remote->m_nState == CRemotePlayer::PLAYER_STATE_WASTED)
        {
            m_spectating.m_nType = SPEC_TYPE_NONE;
            m_spectating.m_nObject = -1;
            m_spectating.m_bProcessed = FALSE;
        }
    }

    if (m_pPed && m_pPed->HasNativePed())
    {
        CMatrix matrix = CMatrix::Unity();
        camera->GetMatrix(&matrix);
        auto position = matrix.GetPosition();
        position.z += 20.0f;
        m_pPed->SetHealth(100.0f);
        m_pPed->Teleport(position);
    }

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

void CLocalPlayer::SendTakeDamage(int id, float damage, int type, int bodyPart)
{
    if (g_netGame && id >= 0 && id <= INVALID_ID && std::isfinite(damage) && damage >= 0.0f && type >= 0 && bodyPart >= 0)
        g_netGame->GiveTakeDamage(true, static_cast<ID>(id), damage * 0.33f, static_cast<std::uint32_t>(type), static_cast<std::uint32_t>(bodyPart));
}

void CLocalPlayer::SendGiveDamage(int id, float damage, int type, int bodyPart)
{
    if (g_netGame && id >= 0 && id <= INVALID_ID && std::isfinite(damage) && damage >= 0.0f && type >= 0 && bodyPart >= 0)
        g_netGame->GiveTakeDamage(false, static_cast<ID>(id), damage * 0.33f, static_cast<std::uint32_t>(type), static_cast<std::uint32_t>(bodyPart));
}

bool CLocalPlayer::ProcessUnoccupiedSync(ID vehicleId, CVehicle* vehicle, float distance)
{
    if (!vehicle || vehicleId >= CVehiclePool::MAX_VEHICLES || !vehicle->HasNativeVehicle() || !m_pPed || !m_pPed->HasNativePed() || m_pPed->IsDead() || !m_bIsActive ||
        m_bDoesSpectating || !g_netGame || g_netGame->GetState() != CNetGame::GAME_MODE_CONNECTED || vehicle->HasPlayerDriver())
        return false;

    if (distance < 0.0f)
        distance = vehicle->GetDistanceToLocalPlayer();
    if (vehicle->IsTrainPart() || !std::isfinite(distance) || distance >= 90.0f || vehicle->IsStationary() || vehicle->GetTractor())
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
    const int lastId = std::min(CPlayerPool::MAX_PLAYERS - 1, std::max(static_cast<int>(localId), playerPool->m_nLargestId));
    float nearestDistance = 100000.0f;
    ID nearestId = CPlayerPool::INVALID_ID;

    CMatrix vehicleMatrix = CMatrix::Unity();
    vehicle->GetMatrix(&vehicleMatrix);
    const CVector vehiclePosition = vehicleMatrix.GetPosition();
    for (int id = 0; id <= lastId; ++id)
    {
        auto* remote = id == localId ? nullptr : playerPool->GetPlayer(static_cast<CPlayerPool::ID>(id));
        auto* ped = id == localId ? m_pPed : remote ? remote->m_pPed : nullptr;
        if (!ped || !ped->HasNativePed() || !ped->IsValid())
            continue;

        const float candidateDistance = ped->GetDistanceToPoint(vehiclePosition);
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
    if (!m_pPed || !m_pPed->HasNativePed() || m_pPed->IsDead() || m_pPed->IsInVehicle() || m_pPed->HasVehicleEntryTask() || !vehiclePool || m_bDoesSpectating ||
        m_classSelection.m_bIsActive || !(m_bIsActive || g_settings.m_bDebugMode) || game::inputhandler::game_input_blocked() || CGame::IsPaused() ||
        (menuPool && menuPool->m_nCurrent != CMenuPool::INVALID_MENU) || (!g_game || GetForegroundWindow() != static_cast<HWND>(g_game->GetWindowHandle())))
        return;
    const ID id = vehiclePool->GetNearest();
    auto* vehicle = id != CVehiclePool::INVALID_ID ? vehiclePool->Get(id) : nullptr;
    if (!vehicle || !vehicle->HasNativeVehicle() || !vehicle->m_handle || !(m_pPed->GetDistanceToEntity(vehicle) < 4.5f))
        return;

    // РћРїРєРѕРґ 0x0470 (get_current_char_weapon): РѕС‚СЂРёРјСѓС” ID РїРѕС‚РѕС‡РЅРѕС— Р·Р±СЂРѕС— РїРµСЂСЃРѕРЅР°Р¶Р°
    int weapon = 0;
    constexpr scm::OpcodeInfo opGetWeapon{0x0470, "iv"};
    scm::ProcessCommand(&opGetWeapon, m_pPed->m_handle, &weapon);
    if (weapon == 46)
    {
        // РћРїРєРѕРґ 0x01B9 (set_char_current_weapon): РїСЂРёРјСѓСЃРѕРІРѕ Р·РјС–РЅСЋС” РїРѕС‚РѕС‡РЅСѓ Р·Р±СЂРѕСЋ РїРµСЂСЃРѕРЅР°Р¶Р° (РІ РґР°РЅРѕРјСѓ РІРёРїР°РґРєСѓ СЃРєРёРґР°С” РїР°СЂР°С€СѓС‚/Р·Р±СЂРѕСЋ РЅР° 0 - РєСѓР»Р°РєРё)
        constexpr scm::OpcodeInfo opSetWeapon{0x01B9, "ii"};
        scm::ProcessCommand(&opSetWeapon, m_pPed->m_handle, 0);
    }
    m_pPed->EnterVehicle(vehicle->m_handle, TRUE);
    EnterVehicle(id, TRUE);
}

void CLocalPlayer::ProcessPassengerEntry(const CPad& keys)
{
    const bool keyDown = (GetAsyncKeyState('G') & 0x8000) != 0;
    const bool pressed = keyDown && !m_passengerKeyDown;
    m_passengerKeyDown = keyDown;
    if (!keys.DisablePlayerControls && !keys.bDisablePlayerEnterCar && (pressed || (keys.NewState.DPadUp && !keys.OldState.DPadUp)))
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
    PROFILE_SCOPE(LocalPlayer);
    if (auto* game = g_game.get())
        m_pPed = game->GetPlayerPed();
    if (!m_pPed || !m_pPed->HasNativePed())
        return;
    m_pPed->ProcessAnimation();

    CPad snapshot{};
    if (const auto* internalKeys = KeyStuff::GetInternalKeys())
    {
        std::memcpy(&snapshot, internalKeys, sizeof(snapshot));
        ProcessPassengerEntry(snapshot);
    }

    const TICK now = GetTickCount();
    if (!m_bIsActive && m_pPed->GetDrunkLevel() > 0)
        m_pPed->SetDrunkLevel(0);
    if (m_bDoesSpectating)
    {
        ProcessSpectating();
        m_bPassengerDriveBy = FALSE;
        return;
    }

    if (m_bIsWasted)
    {
        // r5 waits for gta to finish death and resurrection before spawning
        if (m_pPed->GetState() == 54 || m_pPed->GetState() == 55)
            return;

        if (m_bClearedToSpawn && !m_classSelection.m_bEnableAfterDeath && g_netGame && g_netGame->GetState() == CNetGame::GAME_MODE_CONNECTED)
        {
            if (m_pPed->GetHealth() > 0.0f)
                Spawn();
        }
        else if (g_netGame)
        {
            m_bIsWasted = FALSE;
            m_classSelection.m_bEnableAfterDeath = FALSE;
            g_netGame->SpawnScreen();
            RequestClass(m_classSelection.m_nSelected);
        }
        return;
    }

    if (!m_bIsActive)
    {
        ProcessClassSelection();
        return;
    }

    if (m_pPed->IsDead() || m_pPed->GetState() == 54)
    {
        if (!m_bIsWasted)
        {
            WastedNotification();
            m_pPed->SetDrunkLevel(0);
            m_bIsActive = FALSE;
            m_bIsWasted = TRUE;
        }
        return;
    }

    const void* currentNativeVehicle = m_pPed->GetVehicle();
    auto* vehiclePool = g_netGame ? g_netGame->GetVehiclePool() : nullptr;
    const ID currentVehicleId = currentNativeVehicle && vehiclePool ? vehiclePool->Find(const_cast<void*>(currentNativeVehicle)) : INVALID_ID;
    const bool entering = m_pPed->HasVehicleEntryTask();
    const bool exiting = m_pPed->HasVehicleExitTask();
    const TICK taskTime = g_game ? static_cast<TICK>(g_game->GetTimer()) : now;
    if (m_vehicleExitPending && !exiting && (!m_pPed->IsInVehicle() || taskTime - m_vehicleTransitionStarted >= 250))
        m_vehicleExitPending = false;
    if (m_vehicleEntryPending && !entering && m_pPed->IsInVehicle() && currentVehicleId == m_nLastVehicle)
        m_vehicleEntryPending = false;
    if (m_nLastVehicle != INVALID_ID && !entering && !exiting &&
        ((m_vehicleEntryPending && !m_pPed->IsInVehicle() && taskTime - m_vehicleTransitionStarted > 100) || (!m_vehicleEntryPending && currentVehicleId != m_nLastVehicle)))
        ExitVehicle(m_nLastVehicle);
    if (!m_vehicleEntryPending && !m_vehicleExitPending && !entering && !exiting && m_pPed->IsInVehicle() && currentVehicleId != INVALID_ID && m_nLastVehicle != currentVehicleId)
        EnterVehicle(currentVehicleId, m_pPed->IsPassenger());

    if (m_pPed->IsInVehicle() && m_pPed->GetVehicleSeatIndex() >= 0)
    {
        if (m_pPed->IsPassenger())
        {
            if (!m_bPassengerDriveBy && snapshot.NewState.ShockButtonL && !snapshot.DisablePlayerControls)
                m_bPassengerDriveBy = m_pPed->StartPassengerDriveBy();
            if (m_bPassengerDriveBy && (!m_pPed->GetCurrentWeapon() || !m_pPed->HasAmmoForCurrentWeapon()))
            {
                m_pPed->StopPassengerDriveBy();
                m_bPassengerDriveBy = FALSE;
            }
            if (now - m_lastUpdate > static_cast<TICK>(GetIncarSendRate()))
            {
                m_lastUpdate = now;
                SendPassengerData();
            }
        }
        else
        {
            m_bPassengerDriveBy = FALSE;
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
    }
    else
    {
        m_bPassengerDriveBy = FALSE;
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
    m_pPed->ProcessDrunk();
    if (m_pPed->DoesDancing())
        m_pPed->ProcessDancing();
    if (m_pPed->IsHoldingStuff())
        m_pPed->ApplyStuff();
    UpdateWeapons();
    UpdateCameraTarget();
    if (g_game)
    {
        const char interior = g_game->GetCurrentInterior();
        if (interior != m_nCurrentInterior)
            ChangeInterior(interior);
    }
}
