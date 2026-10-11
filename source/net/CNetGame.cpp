#include <stdafx.h>

#include <new>

#include "CNetGame.h"
#include "authentication.h"
#include "RPCHandlers.h"
#include "CSrvNetStats.h"

#include "main.h"

#include "game/CGame.h"
#include "game/BuildingRemoval.h"

#include "gui/CChat.h"
#include "gui/CDialog.h"
#include "gui/CObjectEdit.h"
#include "gui/CObjectSelection.h"
#include "gui/CTextDrawSelection.h"

#include "players/CRemotePlayer.h"
#include "players/PlayerColors.h"

#include "render/render.h"

#include "sync/KeyStuff.h"
#include "sync/AimStuff.h"

// RakNet
#include <RakClient.h>
#include <PacketEnumerations.h>
#include <BitStream.h>

// RakNet namespace
using namespace RakNet;

bool g_DLClient = true;

namespace
{
    // client -> server
    int g_ClickPlayer = 23;
    int g_ClientJoin = 25;
    int g_EnterVehicle = 26;
    int g_SelectObject = 27;
    int g_ClientCheckResponse = 103;
    int g_ExitVehicle = 154;
    int g_SendCommand = 50;
    int g_SendSpawn = 52;
    int g_DeathNotification = 53;
    //int g_NPCJoin = 54;
    int g_DialogResponse = 62;
    int g_SelectTextDraw = 83;
    int g_VehicleDamaged = 106;
    int g_GiveTakeDamage = 115;
    int g_InteriorChangeNotification = 118;
    int g_MapMarker = 119;
    int g_PickedUpPickup = 131;
    int g_MenuSelect = 132;
    int g_VehicleDestroyed = 136;
    int g_MenuQuit = 140;
    int g_CameraTarget = 168;
    int g_GiveActorDamage = 177;

    // server -> client
    int g_SetPlayerVirtualWorld = 48;   // 0.3.DL
    int g_ModelInfo = 179;              // 0.3.DL
    int g_ModelFile = 183;              // 0.3.DL
    int g_SrcFinishedDownloading = 185; // 0.3.DL
    int g_RequestDffFile = 181;         // 0.3.DL client -> server
    int g_RequestTxdFile = 182;         // 0.3.DL client -> server
    int g_FinishedDownloading = 184;    // 0.3.DL client -> server

    int g_SetPlayerName = 11;
    int g_SetPlayerPos = 12;
    int g_SetPlayerPosFindZ = 13;
    int g_SetPlayerHealth = 14;
    int g_TogglePlayerControllable = 15;
    int g_PlaySound = 16;
    int g_SetWorldBounds = 17;
    int g_GivePlayerMoney = 18;
    int g_SetPlayerFacingAngle = 19;
    int g_ResetPlayerMoney = 20;
    int g_ResetPlayerWeapons = 21;
    int g_GivePlayerWeapon = 22;

    int g_SetVehicleParamsEx = 24;

    int g_PlayerEnterVehicle = 26;
    int g_EnterEditObject = 27;
    int g_CancelEdit = 28;
    int g_SetPlayerTime = 29;
    int g_ToggleClock = 30;

    int g_WorldPlayerAdd = 32;
    int g_SetShopName = 33;
    int g_SetPlayerSkillLevel = 34;
    int g_SetPlayerDrunkLevel = 35;
    int g_Create3DTextLabel = 36;
    int g_DisableCheckpoint = 37;
    int g_SetRaceCheckpoint = 38;
    int g_DisableRaceCheckpoint = 39;
    int g_GameModeRestart = 40;
    int g_PlayAudioStream = 41;
    int g_StopAudioStream = 42;
    int g_RemoveBuilding = 43;
    int g_CreateObject = 44;
    int g_SetObjectPos = 45;
    int g_SetObjectRotation = 46;
    int g_DestroyObject = 47;

    int g_SendDeathMessage = 55;
    int g_SetMapIcon = 56;
    int g_RemoveVehicleComponent = 57;
    int g_Delete3DTextLabel = 58;
    int g_ChatBubble = 59;
    int g_SendGameTimeUpdate = 60;
    int g_ShowDialog = 61;

    int g_DestroyPickup = 63;

    int g_LinkVehicleToInterior = 65;
    int g_SetPlayerArmour = 66;
    int g_SetArmedWeapon = 67;
    int g_SetSpawnInfo = 68;
    int g_SetPlayerTeam = 69;
    int g_PutPlayerInVehicle = 70;
    int g_RemovePlayerFromVehicle = 71;
    int g_SetPlayerColor = 72;
    int g_ShowGameText = 73;
    int g_ForceClassSelection = 74;
    int g_AttachObjectToPlayer = 75;
    int g_InitMenu = 76;
    int g_ShowMenu = 77;
    int g_HideMenu = 78;
    int g_CreateExplosion = 79;
    int g_ShowPlayerNameTag = 80;
    int g_AttachCameraToObject = 81;
    int g_InterpolateCamera = 82;
    int g_ToggleSelectTextDraw = 83;
    int g_SetPlayerObjectMaterial = 84;
    int g_GangZoneStopFlash = 85;
    int g_ApplyPlayerAnimation = 86;
    int g_ClearPlayerAnimation = 87;
    int g_SetPlayerSpecialAction = 88;
    int g_SetPlayerFightingStyle = 89;
    int g_SetPlayerVelocity = 90;
    int g_SetVehicleVelocity = 91;

    int g_SendClientMessage = 93;
    int g_SetWorldTime = 94;
    int g_CreatePickup = 95;
    int g_ScmEvent = 96;

    int g_SetVehicleTireStatus = 98;
    int g_MoveObject = 99;

    int g_ChatMessage = 101;
    int g_SrvNetStats = 102; // legacy
    int g_ClientCheck = 103;
    int g_EnableStuntBonus = 104;
    int g_TextDrawSetString = 105;
    int g_UpdateVehicleDamageStatus = 106;
    int g_SetCheckpoint = 107;
    int g_AddGangZone = 108;

    int g_ToggleWidescreen = 111;
    int g_PlayCrimeReport = 112;
    int g_SetPlayerAttachedObject = 113;

    int g_EditAttachedObject = 116;
    int g_EditObject = 117;

    int g_GangZoneDestroy = 120;
    int g_GangZoneFlash = 121;
    int g_StopObject = 122;
    int g_SetVehicleNumberPlate = 123;
    int g_TogglePlayerSpectating = 124;

    int g_SpectatePlayer = 126;
    int g_SpectateVehicle = 127;
    int g_RequestClass = 128;
    int g_RequestSpawn = 129;
    int g_ConnectionRejected = 130;

    int g_SetPlayerWantedLevel = 133;
    int g_ShowTextDraw = 134;
    int g_HideTextDraw = 135;

    int g_ServerJoin = 137;
    int g_ServerQuit = 138;
    int g_InitGame = 139;

    int g_RemoveMapIcon = 144;
    int g_SetWeaponAmmo = 145;
    int g_SetGravity = 146;
    int g_SetVehicleHealth = 147;
    int g_AttachTrailerToVehicle = 148;
    int g_DetachTrailerFromVehicle = 149;

    int g_DestroyWeaponPickup = 151; // legacy
    int g_SetWeather = 152;
    int g_SetPlayerSkin = 153;
    int g_PlayerExitVehicle = 154;
    int g_UpdateScoresAndPings = 155;
    int g_SetInterior = 156;
    int g_SetCameraPos = 157;
    int g_SetCameraLookAt = 158;
    int g_SetVehiclePos = 159;
    int g_SetVehicleZAngle = 160;
    int g_SetVehicleParams = 161;
    int g_SetCameraBehind = 162;
    int g_WorldPlayerRemove = 163;
    int g_WorldVehicleAdd = 164;
    int g_WorldVehicleRemove = 165;
    int g_DeathBroadcast = 166;
    int g_DisableVehicleCollisions = 167;

    // int g_SetPlayerObjectNoCameraCol = 169; // legacy
    int g_ToggleCameraTarget = 170;
    int g_ShowActor = 171;
    int g_HideActor = 172;
    int g_ApplyActorAnimation = 173;
    int g_ClearActorAnimation = 174;
    int g_SetActorFacingAngle = 175;
    int g_SetActorPos = 176;

    int g_SetActorHealth = 178;

    std::uint32_t g_lastPlayersUpdateRequest = 0;
    std::uint32_t g_lagCompMode = 0;

    std::uint32_t GetTimeNow()
    {
        return ::GetTickCount();
    }

    template <class T>
    void ResetPool(T*& pool)
    {
        T* replacement = new T();
        delete pool;
        pool = replacement;
    }

    void DestroyPools(CNetGame::Pools*& pools)
    {
        if (!pools)
            return;

        delete pools->m_pPlayer;
        delete pools->m_pMenu;
        delete pools->m_pTextDraw;
        delete pools->m_pLabel;
        delete pools->m_pGangZone;
        delete pools->m_pActor;
        delete pools->m_pObject;
        delete pools->m_pPickup;
        delete pools->m_pVehicle;
        delete pools;
        pools = nullptr;
    }

    CRemotePlayer* ResolveRemote(CNetGame* netGame, CNetGame::ID id)
    {
        auto* pool = netGame ? netGame->GetPlayerPool() : nullptr;
        return pool ? pool->GetPlayer(id) : nullptr;
    }

    bool ReadSyncHeader(BitStream& bs, std::uint8_t expectedId, std::uint32_t& timestamp)
    {
        std::uint8_t packetId{};
        timestamp = ::GetTickCount();
        if (!bs.Read(packetId))
            return false;

        if (packetId == ID_TIMESTAMP)
        {
            if (!bs.Read(timestamp) || !bs.Read(packetId))
                return false;
        }
        return packetId == expectedId;
    }

    template <typename T>
    bool ReadSyncBody(BitStream& bs, T& data)
    {
        return bs.GetNumberOfUnreadBits() >= sizeof(T) * 8u && bs.Read(reinterpret_cast<char*>(&data), static_cast<int>(sizeof(T)));
    }

    std::uint8_t UnpackHealth(std::uint8_t nibble)
    {
        if (nibble == 15)
            return 100;
        return static_cast<std::uint8_t>(nibble * 7u);
    }

    void ReadHealthArmor(std::uint8_t packed, std::uint8_t& health, std::uint8_t& armor)
    {
        health = UnpackHealth(packed >> 4);
        armor = UnpackHealth(packed & 15u);
    }

    bool ReadOnfootData(BitStream& bs, Synchronization::OnfootData& data)
    {
        bool hasLeftRight{};
        bool hasUpDown{};
        if (!bs.Read(hasLeftRight))
            return false;
        if (hasLeftRight && !bs.Read(data.m_controllerState.m_sLeftStickX))
            return false;
        if (!bs.Read(hasUpDown))
            return false;
        if (hasUpDown && !bs.Read(data.m_controllerState.m_sLeftStickY))
            return false;

        std::uint8_t healthArmor{};
        if (!bs.Read(data.m_controllerState.m_value) || !bs.Read(reinterpret_cast<char*>(&data.m_position), static_cast<int>(sizeof(data.m_position))) ||
            !bs.ReadNormQuat(data.m_fQuaternion[0], data.m_fQuaternion[1], data.m_fQuaternion[2], data.m_fQuaternion[3]) || !bs.Read(healthArmor) ||
            !bs.Read(data.m_nCurrentWeapon) || !bs.Read(data.m_nSpecialAction) || !bs.ReadVector(data.m_speed.x, data.m_speed.y, data.m_speed.z))
            return false;
        ReadHealthArmor(healthArmor, data.m_nHealth, data.m_nArmor);

        data.m_nSurfingVehicleId = CRemotePlayer::INVALID_ID;
        bool hasSurfing{};
        if (!bs.Read(hasSurfing))
            return false;
        if (hasSurfing && (!bs.Read(data.m_nSurfingVehicleId) || !bs.Read(reinterpret_cast<char*>(&data.m_surfingOffset), static_cast<int>(sizeof(data.m_surfingOffset)))))
            return false;

        bool hasAnimation{};
        if (!bs.Read(hasAnimation))
            return false;
        if (hasAnimation)
        {
            std::uint16_t animationId{};
            std::uint16_t animationFlags{};
            if (!bs.Read(animationId) || !bs.Read(animationFlags))
                return false;
            data.m_animation.m_value = static_cast<std::uint32_t>(animationId) | (static_cast<std::uint32_t>(animationFlags) << 16);
        }
        return true;
    }

    bool ReadIncarData(BitStream& bs, Synchronization::IncarData& data)
    {
        std::uint16_t vehicleHealth{};
        std::uint8_t healthArmor{};
        if (!bs.Read(data.m_nVehicle) || !bs.Read(data.m_controllerState.m_sLeftStickX) || !bs.Read(data.m_controllerState.m_sLeftStickY) ||
            !bs.Read(data.m_controllerState.m_value) || !bs.ReadNormQuat(data.m_fQuaternion[0], data.m_fQuaternion[1], data.m_fQuaternion[2], data.m_fQuaternion[3]) ||
            !bs.Read(reinterpret_cast<char*>(&data.m_position), static_cast<int>(sizeof(data.m_position))) || !bs.ReadVector(data.m_speed.x, data.m_speed.y, data.m_speed.z) ||
            !bs.Read(vehicleHealth) || !bs.Read(healthArmor) || !bs.Read(data.m_nCurrentWeapon) || !bs.Read(data.m_bSirenEnabled) || !bs.Read(data.m_bLandingGear))
            return false;

        data.m_fHealth = static_cast<float>(vehicleHealth);
        ReadHealthArmor(healthArmor, data.m_nDriverHealth, data.m_nDriverArmor);

        bool hasExtra{};
        if (!bs.Read(hasExtra))
            return false;
        if (hasExtra && !bs.Read(data.m_fTrainSpeed))
            return false;

        data.m_nTrailerId = CRemotePlayer::INVALID_ID;
        bool hasTrailer{};
        if (!bs.Read(hasTrailer))
            return false;
        return !hasTrailer || bs.Read(data.m_nTrailerId);
    }
}

std::uint32_t& CNetGame::RefLastPlayersUpdateRequest()
{
    return g_lastPlayersUpdateRequest;
}

std::uint32_t& CNetGame::RefLagCompMode()
{
    return g_lagCompMode;
}

void CNetGame::RegisterRPC()
{
    m_pRakClient->RegisterAsRemoteProcedureCall(&g_SetPlayerName, RPCHandlers::SetPlayerName);
    m_pRakClient->RegisterAsRemoteProcedureCall(&g_SetPlayerPos, RPCHandlers::SetPlayerPos);
    m_pRakClient->RegisterAsRemoteProcedureCall(&g_SetPlayerPosFindZ, RPCHandlers::SetPlayerPosFindZ);
    m_pRakClient->RegisterAsRemoteProcedureCall(&g_SetPlayerHealth, RPCHandlers::SetPlayerHealth);
    m_pRakClient->RegisterAsRemoteProcedureCall(&g_TogglePlayerControllable, RPCHandlers::TogglePlayerControllable);
    m_pRakClient->RegisterAsRemoteProcedureCall(&g_PlaySound, RPCHandlers::PlaySound);
    m_pRakClient->RegisterAsRemoteProcedureCall(&g_SetWorldBounds, RPCHandlers::SetWorldBounds);
    m_pRakClient->RegisterAsRemoteProcedureCall(&g_GivePlayerMoney, RPCHandlers::GivePlayerMoney);
    m_pRakClient->RegisterAsRemoteProcedureCall(&g_SetPlayerFacingAngle, RPCHandlers::SetPlayerFacingAngle);
    m_pRakClient->RegisterAsRemoteProcedureCall(&g_ResetPlayerMoney, RPCHandlers::ResetPlayerMoney);
    m_pRakClient->RegisterAsRemoteProcedureCall(&g_ResetPlayerWeapons, RPCHandlers::ResetPlayerWeapons);
    m_pRakClient->RegisterAsRemoteProcedureCall(&g_GivePlayerWeapon, RPCHandlers::GivePlayerWeapon);

    m_pRakClient->RegisterAsRemoteProcedureCall(&g_SetVehicleParamsEx, RPCHandlers::SetVehicleParamsEx);

    m_pRakClient->RegisterAsRemoteProcedureCall(&g_PlayerEnterVehicle, RPCHandlers::PlayerEnterVehicle);
    m_pRakClient->RegisterAsRemoteProcedureCall(&g_EnterEditObject, RPCHandlers::EnterEditObject);
    m_pRakClient->RegisterAsRemoteProcedureCall(&g_CancelEdit, RPCHandlers::CancelEdit);
    m_pRakClient->RegisterAsRemoteProcedureCall(&g_SetPlayerTime, RPCHandlers::SetPlayerTime);
    m_pRakClient->RegisterAsRemoteProcedureCall(&g_ToggleClock, RPCHandlers::ToggleClock);

    m_pRakClient->RegisterAsRemoteProcedureCall(&g_WorldPlayerAdd, RPCHandlers::WorldPlayerAdd);
    m_pRakClient->RegisterAsRemoteProcedureCall(&g_SetShopName, RPCHandlers::SetShopName);
    m_pRakClient->RegisterAsRemoteProcedureCall(&g_SetPlayerSkillLevel, RPCHandlers::SetPlayerSkillLevel);
    m_pRakClient->RegisterAsRemoteProcedureCall(&g_SetPlayerDrunkLevel, RPCHandlers::SetPlayerDrunkLevel);
    m_pRakClient->RegisterAsRemoteProcedureCall(&g_Create3DTextLabel, RPCHandlers::Create3DTextLabel);
    m_pRakClient->RegisterAsRemoteProcedureCall(&g_DisableCheckpoint, RPCHandlers::DisableCheckpoint);
    m_pRakClient->RegisterAsRemoteProcedureCall(&g_SetRaceCheckpoint, RPCHandlers::SetRaceCheckpoint);
    m_pRakClient->RegisterAsRemoteProcedureCall(&g_DisableRaceCheckpoint, RPCHandlers::DisableRaceCheckpoint);
    m_pRakClient->RegisterAsRemoteProcedureCall(&g_GameModeRestart, RPCHandlers::GameModeRestart);
    m_pRakClient->RegisterAsRemoteProcedureCall(&g_PlayAudioStream, RPCHandlers::PlayAudioStream);
    m_pRakClient->RegisterAsRemoteProcedureCall(&g_StopAudioStream, RPCHandlers::StopAudioStream);
    m_pRakClient->RegisterAsRemoteProcedureCall(&g_RemoveBuilding, RPCHandlers::RemoveBuilding);
    m_pRakClient->RegisterAsRemoteProcedureCall(&g_CreateObject, RPCHandlers::CreateObject);
    m_pRakClient->RegisterAsRemoteProcedureCall(&g_SetObjectPos, RPCHandlers::SetObjectPos);
    m_pRakClient->RegisterAsRemoteProcedureCall(&g_SetObjectRotation, RPCHandlers::SetObjectRotation);
    m_pRakClient->RegisterAsRemoteProcedureCall(&g_DestroyObject, RPCHandlers::DestroyObject);

    m_pRakClient->RegisterAsRemoteProcedureCall(&g_SendDeathMessage, RPCHandlers::SendDeathMessage);
    m_pRakClient->RegisterAsRemoteProcedureCall(&g_SetMapIcon, RPCHandlers::SetMapIcon);
    m_pRakClient->RegisterAsRemoteProcedureCall(&g_RemoveVehicleComponent, RPCHandlers::RemoveVehicleComponent);
    m_pRakClient->RegisterAsRemoteProcedureCall(&g_Delete3DTextLabel, RPCHandlers::Delete3DTextLabel);
    m_pRakClient->RegisterAsRemoteProcedureCall(&g_ChatBubble, RPCHandlers::ChatBubble);
    m_pRakClient->RegisterAsRemoteProcedureCall(&g_SendGameTimeUpdate, RPCHandlers::SendGameTimeUpdate);
    m_pRakClient->RegisterAsRemoteProcedureCall(&g_ShowDialog, RPCHandlers::ShowDialog);

    m_pRakClient->RegisterAsRemoteProcedureCall(&g_DestroyPickup, RPCHandlers::DestroyPickup);

    m_pRakClient->RegisterAsRemoteProcedureCall(&g_LinkVehicleToInterior, RPCHandlers::LinkVehicleToInterior);
    m_pRakClient->RegisterAsRemoteProcedureCall(&g_SetPlayerArmour, RPCHandlers::SetPlayerArmour);
    m_pRakClient->RegisterAsRemoteProcedureCall(&g_SetArmedWeapon, RPCHandlers::SetArmedWeapon);
    m_pRakClient->RegisterAsRemoteProcedureCall(&g_SetSpawnInfo, RPCHandlers::SetSpawnInfo);
    m_pRakClient->RegisterAsRemoteProcedureCall(&g_SetPlayerTeam, RPCHandlers::SetPlayerTeam);
    m_pRakClient->RegisterAsRemoteProcedureCall(&g_PutPlayerInVehicle, RPCHandlers::PutPlayerInVehicle);
    m_pRakClient->RegisterAsRemoteProcedureCall(&g_RemovePlayerFromVehicle, RPCHandlers::RemovePlayerFromVehicle);
    m_pRakClient->RegisterAsRemoteProcedureCall(&g_SetPlayerColor, RPCHandlers::SetPlayerColor);
    m_pRakClient->RegisterAsRemoteProcedureCall(&g_ShowGameText, RPCHandlers::ShowGameText);
    m_pRakClient->RegisterAsRemoteProcedureCall(&g_ForceClassSelection, RPCHandlers::ForceClassSelection);
    m_pRakClient->RegisterAsRemoteProcedureCall(&g_AttachObjectToPlayer, RPCHandlers::AttachObjectToPlayer);
    m_pRakClient->RegisterAsRemoteProcedureCall(&g_InitMenu, RPCHandlers::InitMenu);
    m_pRakClient->RegisterAsRemoteProcedureCall(&g_ShowMenu, RPCHandlers::ShowMenu);
    m_pRakClient->RegisterAsRemoteProcedureCall(&g_HideMenu, RPCHandlers::HideMenu);
    m_pRakClient->RegisterAsRemoteProcedureCall(&g_CreateExplosion, RPCHandlers::CreateExplosion);
    m_pRakClient->RegisterAsRemoteProcedureCall(&g_ShowPlayerNameTag, RPCHandlers::ShowPlayerNameTag);
    m_pRakClient->RegisterAsRemoteProcedureCall(&g_AttachCameraToObject, RPCHandlers::AttachCameraToObject);
    m_pRakClient->RegisterAsRemoteProcedureCall(&g_InterpolateCamera, RPCHandlers::InterpolateCamera);
    m_pRakClient->RegisterAsRemoteProcedureCall(&g_ToggleSelectTextDraw, RPCHandlers::ToggleSelectTextDraw);
    m_pRakClient->RegisterAsRemoteProcedureCall(&g_SetPlayerObjectMaterial, RPCHandlers::SetPlayerObjectMaterial);
    m_pRakClient->RegisterAsRemoteProcedureCall(&g_GangZoneStopFlash, RPCHandlers::GangZoneStopFlash);
    m_pRakClient->RegisterAsRemoteProcedureCall(&g_ApplyPlayerAnimation, RPCHandlers::ApplyPlayerAnimation);
    m_pRakClient->RegisterAsRemoteProcedureCall(&g_ClearPlayerAnimation, RPCHandlers::ClearPlayerAnimation);
    m_pRakClient->RegisterAsRemoteProcedureCall(&g_SetPlayerSpecialAction, RPCHandlers::SetPlayerSpecialAction);
    m_pRakClient->RegisterAsRemoteProcedureCall(&g_SetPlayerFightingStyle, RPCHandlers::SetPlayerFightingStyle);
    m_pRakClient->RegisterAsRemoteProcedureCall(&g_SetPlayerVelocity, RPCHandlers::SetPlayerVelocity);
    m_pRakClient->RegisterAsRemoteProcedureCall(&g_SetVehicleVelocity, RPCHandlers::SetVehicleVelocity);

    m_pRakClient->RegisterAsRemoteProcedureCall(&g_SendClientMessage, RPCHandlers::SendClientMessage);
    m_pRakClient->RegisterAsRemoteProcedureCall(&g_SetWorldTime, RPCHandlers::SetWorldTime);
    m_pRakClient->RegisterAsRemoteProcedureCall(&g_CreatePickup, RPCHandlers::CreatePickup);
    m_pRakClient->RegisterAsRemoteProcedureCall(&g_ScmEvent, RPCHandlers::ScmEvent);

    m_pRakClient->RegisterAsRemoteProcedureCall(&g_SetVehicleTireStatus, RPCHandlers::SetVehicleTireStatus);
    m_pRakClient->RegisterAsRemoteProcedureCall(&g_MoveObject, RPCHandlers::MoveObject);

    m_pRakClient->RegisterAsRemoteProcedureCall(&g_ChatMessage, RPCHandlers::ChatMessage);
    m_pRakClient->RegisterAsRemoteProcedureCall(&g_SrvNetStats, RPCHandlers::SrvNetStats);
    m_pRakClient->RegisterAsRemoteProcedureCall(&g_ClientCheck, RPCHandlers::ClientCheck);
    m_pRakClient->RegisterAsRemoteProcedureCall(&g_EnableStuntBonus, RPCHandlers::EnableStuntBonus);
    m_pRakClient->RegisterAsRemoteProcedureCall(&g_TextDrawSetString, RPCHandlers::TextDrawSetString);
    m_pRakClient->RegisterAsRemoteProcedureCall(&g_UpdateVehicleDamageStatus, RPCHandlers::UpdateVehicleDamageStatus);
    m_pRakClient->RegisterAsRemoteProcedureCall(&g_SetCheckpoint, RPCHandlers::SetCheckpoint);
    m_pRakClient->RegisterAsRemoteProcedureCall(&g_AddGangZone, RPCHandlers::AddGangZone);

    m_pRakClient->RegisterAsRemoteProcedureCall(&g_ToggleWidescreen, RPCHandlers::ToggleWidescreen);
    m_pRakClient->RegisterAsRemoteProcedureCall(&g_PlayCrimeReport, RPCHandlers::PlayCrimeReport);
    m_pRakClient->RegisterAsRemoteProcedureCall(&g_SetPlayerAttachedObject, RPCHandlers::SetPlayerAttachedObject);

    m_pRakClient->RegisterAsRemoteProcedureCall(&g_EditAttachedObject, RPCHandlers::EditAttachedObject);
    m_pRakClient->RegisterAsRemoteProcedureCall(&g_EditObject, RPCHandlers::EditObject);

    m_pRakClient->RegisterAsRemoteProcedureCall(&g_GangZoneDestroy, RPCHandlers::GangZoneDestroy);
    m_pRakClient->RegisterAsRemoteProcedureCall(&g_GangZoneFlash, RPCHandlers::GangZoneFlash);
    m_pRakClient->RegisterAsRemoteProcedureCall(&g_StopObject, RPCHandlers::StopObject);
    m_pRakClient->RegisterAsRemoteProcedureCall(&g_SetVehicleNumberPlate, RPCHandlers::SetVehicleNumberPlate);
    m_pRakClient->RegisterAsRemoteProcedureCall(&g_TogglePlayerSpectating, RPCHandlers::TogglePlayerSpectating);

    m_pRakClient->RegisterAsRemoteProcedureCall(&g_SpectatePlayer, RPCHandlers::SpectatePlayer);
    m_pRakClient->RegisterAsRemoteProcedureCall(&g_SpectateVehicle, RPCHandlers::SpectateVehicle);
    m_pRakClient->RegisterAsRemoteProcedureCall(&g_RequestClass, RPCHandlers::RequestClass);
    m_pRakClient->RegisterAsRemoteProcedureCall(&g_RequestSpawn, RPCHandlers::RequestSpawn);
    m_pRakClient->RegisterAsRemoteProcedureCall(&g_ConnectionRejected, RPCHandlers::ConnectionRejected);

    m_pRakClient->RegisterAsRemoteProcedureCall(&g_SetPlayerWantedLevel, RPCHandlers::SetPlayerWantedLevel);
    m_pRakClient->RegisterAsRemoteProcedureCall(&g_ShowTextDraw, RPCHandlers::ShowTextDraw);
    m_pRakClient->RegisterAsRemoteProcedureCall(&g_HideTextDraw, RPCHandlers::HideTextDraw);

    m_pRakClient->RegisterAsRemoteProcedureCall(&g_ServerJoin, RPCHandlers::ServerJoin);
    m_pRakClient->RegisterAsRemoteProcedureCall(&g_ServerQuit, RPCHandlers::ServerQuit);
    m_pRakClient->RegisterAsRemoteProcedureCall(&g_InitGame, RPCHandlers::InitGame);

    m_pRakClient->RegisterAsRemoteProcedureCall(&g_RemoveMapIcon, RPCHandlers::RemoveMapIcon);
    m_pRakClient->RegisterAsRemoteProcedureCall(&g_SetWeaponAmmo, RPCHandlers::SetWeaponAmmo);
    m_pRakClient->RegisterAsRemoteProcedureCall(&g_SetGravity, RPCHandlers::SetGravity);
    m_pRakClient->RegisterAsRemoteProcedureCall(&g_SetVehicleHealth, RPCHandlers::SetVehicleHealth);
    m_pRakClient->RegisterAsRemoteProcedureCall(&g_AttachTrailerToVehicle, RPCHandlers::AttachTrailerToVehicle);
    m_pRakClient->RegisterAsRemoteProcedureCall(&g_DetachTrailerFromVehicle, RPCHandlers::DetachTrailerFromVehicle);

    m_pRakClient->RegisterAsRemoteProcedureCall(&g_DestroyWeaponPickup, RPCHandlers::DestroyWeaponPickup); // legacy
    m_pRakClient->RegisterAsRemoteProcedureCall(&g_SetWeather, RPCHandlers::SetWeather);
    m_pRakClient->RegisterAsRemoteProcedureCall(&g_SetPlayerSkin, RPCHandlers::SetPlayerSkin);
    m_pRakClient->RegisterAsRemoteProcedureCall(&g_PlayerExitVehicle, RPCHandlers::PlayerExitVehicle);
    m_pRakClient->RegisterAsRemoteProcedureCall(&g_UpdateScoresAndPings, RPCHandlers::UpdateScoresAndPings);
    m_pRakClient->RegisterAsRemoteProcedureCall(&g_SetInterior, RPCHandlers::SetInterior);
    m_pRakClient->RegisterAsRemoteProcedureCall(&g_SetCameraPos, RPCHandlers::SetCameraPos);
    m_pRakClient->RegisterAsRemoteProcedureCall(&g_SetCameraLookAt, RPCHandlers::SetCameraLookAt);
    m_pRakClient->RegisterAsRemoteProcedureCall(&g_SetVehiclePos, RPCHandlers::SetVehiclePos);
    m_pRakClient->RegisterAsRemoteProcedureCall(&g_SetVehicleZAngle, RPCHandlers::SetVehicleZAngle);
    m_pRakClient->RegisterAsRemoteProcedureCall(&g_SetVehicleParams, RPCHandlers::SetVehicleParams);
    m_pRakClient->RegisterAsRemoteProcedureCall(&g_SetCameraBehind, RPCHandlers::SetCameraBehind);
    m_pRakClient->RegisterAsRemoteProcedureCall(&g_WorldPlayerRemove, RPCHandlers::WorldPlayerRemove);
    m_pRakClient->RegisterAsRemoteProcedureCall(&g_WorldVehicleAdd, RPCHandlers::WorldVehicleAdd);
    m_pRakClient->RegisterAsRemoteProcedureCall(&g_WorldVehicleRemove, RPCHandlers::WorldVehicleRemove);
    m_pRakClient->RegisterAsRemoteProcedureCall(&g_DeathBroadcast, RPCHandlers::DeathBroadcast);
    m_pRakClient->RegisterAsRemoteProcedureCall(&g_DisableVehicleCollisions, RPCHandlers::DisableVehicleCollisions);

    // m_pRakClient->RegisterAsRemoteProcedureCall(&g_SetPlayerObjectNoCameraCol, RPCHandlers::SetPlayerObjectNoCameraCol);
    if (g_DLClient)
    {
        m_pRakClient->RegisterAsRemoteProcedureCall(&g_SetPlayerVirtualWorld, RPCHandlers::SetPlayerVirtualWorld);
        m_pRakClient->RegisterAsRemoteProcedureCall(&g_ModelInfo, RPCHandlers::ModelInfo);
        m_pRakClient->RegisterAsRemoteProcedureCall(&g_ModelFile, RPCHandlers::ModelFile);
        m_pRakClient->RegisterAsRemoteProcedureCall(&g_SrcFinishedDownloading, RPCHandlers::SrcFinishedDownloading);
    }

    m_pRakClient->RegisterAsRemoteProcedureCall(&g_ToggleCameraTarget, RPCHandlers::ToggleCameraTarget);
    m_pRakClient->RegisterAsRemoteProcedureCall(&g_ShowActor, RPCHandlers::ShowActor);
    m_pRakClient->RegisterAsRemoteProcedureCall(&g_HideActor, RPCHandlers::HideActor);
    m_pRakClient->RegisterAsRemoteProcedureCall(&g_ApplyActorAnimation, RPCHandlers::ApplyActorAnimation);
    m_pRakClient->RegisterAsRemoteProcedureCall(&g_ClearActorAnimation, RPCHandlers::ClearActorAnimation);
    m_pRakClient->RegisterAsRemoteProcedureCall(&g_SetActorFacingAngle, RPCHandlers::SetActorFacingAngle);
    m_pRakClient->RegisterAsRemoteProcedureCall(&g_SetActorPos, RPCHandlers::SetActorPos);

    m_pRakClient->RegisterAsRemoteProcedureCall(&g_SetActorHealth, RPCHandlers::SetActorHealth);
}

void CNetGame::UnRegisterRPC()
{
    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_SetPlayerName);
    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_SetPlayerPos);
    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_SetPlayerPosFindZ);
    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_SetPlayerHealth);
    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_TogglePlayerControllable);
    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_PlaySound);
    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_SetWorldBounds);
    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_GivePlayerMoney);
    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_SetPlayerFacingAngle);
    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_ResetPlayerMoney);
    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_ResetPlayerWeapons);
    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_GivePlayerWeapon);

    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_SetVehicleParamsEx);

    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_PlayerEnterVehicle);
    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_EnterEditObject);
    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_CancelEdit);
    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_SetPlayerTime);
    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_ToggleClock);

    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_WorldPlayerAdd);
    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_SetShopName);
    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_SetPlayerSkillLevel);
    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_SetPlayerDrunkLevel);
    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_Create3DTextLabel);
    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_DisableCheckpoint);
    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_SetRaceCheckpoint);
    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_DisableRaceCheckpoint);
    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_GameModeRestart);
    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_PlayAudioStream);
    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_StopAudioStream);
    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_RemoveBuilding);
    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_CreateObject);
    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_SetObjectPos);
    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_SetObjectRotation);
    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_DestroyObject);

    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_SendDeathMessage);
    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_SetMapIcon);
    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_RemoveVehicleComponent);
    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_Delete3DTextLabel);
    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_ChatBubble);
    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_SendGameTimeUpdate);
    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_ShowDialog);

    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_DestroyPickup);

    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_LinkVehicleToInterior);
    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_SetPlayerArmour);
    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_SetArmedWeapon);
    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_SetSpawnInfo);
    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_SetPlayerTeam);
    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_PutPlayerInVehicle);
    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_RemovePlayerFromVehicle);
    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_SetPlayerColor);
    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_ShowGameText);
    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_ForceClassSelection);
    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_AttachObjectToPlayer);
    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_InitMenu);
    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_ShowMenu);
    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_HideMenu);
    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_CreateExplosion);
    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_ShowPlayerNameTag);
    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_AttachCameraToObject);
    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_InterpolateCamera);
    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_ToggleSelectTextDraw);
    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_SetPlayerObjectMaterial);
    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_GangZoneStopFlash);
    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_ApplyPlayerAnimation);
    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_ClearPlayerAnimation);
    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_SetPlayerSpecialAction);
    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_SetPlayerFightingStyle);
    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_SetPlayerVelocity);
    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_SetVehicleVelocity);

    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_SendClientMessage);
    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_SetWorldTime);
    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_CreatePickup);
    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_ScmEvent);

    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_SetVehicleTireStatus);
    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_MoveObject);

    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_ChatMessage);
    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_SrvNetStats);
    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_ClientCheck);
    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_EnableStuntBonus);
    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_TextDrawSetString);
    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_UpdateVehicleDamageStatus);
    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_SetCheckpoint);
    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_AddGangZone);

    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_ToggleWidescreen);
    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_PlayCrimeReport);
    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_SetPlayerAttachedObject);

    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_EditAttachedObject);
    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_EditObject);

    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_GangZoneDestroy);
    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_GangZoneFlash);
    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_StopObject);
    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_SetVehicleNumberPlate);
    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_TogglePlayerSpectating);

    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_SpectatePlayer);
    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_SpectateVehicle);
    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_RequestClass);
    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_RequestSpawn);
    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_ConnectionRejected);

    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_SetPlayerWantedLevel);
    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_ShowTextDraw);
    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_HideTextDraw);

    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_ServerJoin);
    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_ServerQuit);
    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_InitGame);

    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_RemoveMapIcon);
    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_SetWeaponAmmo);
    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_SetGravity);
    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_SetVehicleHealth);
    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_AttachTrailerToVehicle);
    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_DetachTrailerFromVehicle);

    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_DestroyWeaponPickup); // legacy
    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_SetWeather);
    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_SetPlayerSkin);
    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_PlayerExitVehicle);
    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_UpdateScoresAndPings);
    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_SetInterior);
    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_SetCameraPos);
    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_SetCameraLookAt);
    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_SetVehiclePos);
    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_SetVehicleZAngle);
    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_SetVehicleParams);
    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_SetCameraBehind);
    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_WorldPlayerRemove);
    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_WorldVehicleAdd);
    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_WorldVehicleRemove);
    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_DeathBroadcast);
    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_DisableVehicleCollisions);

    // m_pRakClient->UnregisterAsRemoteProcedureCall(&g_SetPlayerObjectNoCameraCol);
    if (g_DLClient)
    {
        m_pRakClient->UnregisterAsRemoteProcedureCall(&g_SetPlayerVirtualWorld);
        m_pRakClient->UnregisterAsRemoteProcedureCall(&g_ModelInfo);
        m_pRakClient->UnregisterAsRemoteProcedureCall(&g_ModelFile);
        m_pRakClient->UnregisterAsRemoteProcedureCall(&g_SrcFinishedDownloading);
    }
    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_ToggleCameraTarget);
    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_ShowActor);
    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_HideActor);
    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_ApplyActorAnimation);
    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_ClearActorAnimation);
    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_SetActorFacingAngle);
    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_SetActorPos);

    m_pRakClient->UnregisterAsRemoteProcedureCall(&g_SetActorHealth);
}

CNetGame::CNetGame(const char* hostAddress, int port, const char* nick, const char* password)
    : m_pRakClient(nullptr), pad_0{}, m_szHostAddress{}, m_szHostname{}, m_bDisableCollision(false), m_bUpdateCameraTarget(true), m_bNametagStatus(true), m_nPort(port),
      m_bLanMode(FALSE), m_aMapIcons{}, m_nGameState(GAME_MODE_WAITCONNECT), m_lastConnectAttempt(GetTimeNow()), m_pSettings(new(std::nothrow) Settings{}), pad_2{},
      m_pPools(nullptr)
{
    if (hostAddress)
        strncpy_s(m_szHostAddress, sizeof(m_szHostAddress), hostAddress,
                  _TRUNCATE);                                          // copy m_szHostAddress -> hostAddress
    strncpy_s(m_szHostname, sizeof(m_szHostname), "SA-MP", _TRUNCATE); // set default server name

    Log("[CNetGame::CNetGame] net=%p, host=%s, port=%d, nick=%s", this, m_szHostAddress, m_nPort, nick ? nick : "");

    // initialize sync buffers
    KeyStuff::Initialize();
    AimStuff::Initialize();

    PlayerColors::Initialize();

    InitializeGameLogic(); // init game logic
    InitializePools();     // init pools

    if (m_pPools && m_pPools->m_pPlayer)
        m_pPools->m_pPlayer->SetLocalPlayerName(nick ? nick : "");

    // get RakClientInterface
    m_pRakClient = new RakClient();

    // set server password
    m_pRakClient->SetPassword(password ? password : "");

    // register rpc
    RegisterRPC();
}

CNetGame::~CNetGame()
{
    Log("[CNetGame::~CNetGame] net=%p, pools=%p, transport=%p", this, m_pPools, m_pRakClient);
    ResetMarkers();

    if (m_pRakClient)
    {
        // disconnect
        CNetGame::Disconnect();

        // unregister rpc
        UnRegisterRPC();

        // destroy RakNet
        delete m_pRakClient;
        m_pRakClient = nullptr;
    }

    // destroy pools
    DestroyPools(m_pPools);

    // destroy settings (ip, port, etc.)
    delete m_pSettings;
    m_pSettings = nullptr;
}

void CNetGame::SetState(int value)
{
    if (value == GAME_MODE_WAITCONNECT && m_nGameState == GAME_MODE_CONNECTED)
    {
        ResetObjectPool();
        ResetActorPool();
    }

    if (m_nGameState == GAME_MODE_CONNECTED && value != GAME_MODE_CONNECTED)
    {
        if (g_dialog)
            g_dialog->Hide();
        if (g_textDrawSelection)
            g_textDrawSelection->Disable(false);
        if (g_objectSelection)
            g_objectSelection->Enable(FALSE);
        if (g_objectEdit && g_objectEdit->m_bEnabled)
            g_objectEdit->Disable(FALSE, false);
        if (auto* menu = GetMenuPool())
            menu->Hide(menu->m_nCurrent);
    }
    m_nGameState = value;
}

// init pools
void CNetGame::InitializePools()
{
    Log("[CNetGame::InitializePools] initializing: previous=%p", m_pPools);

    auto* pools = new Pools{};

    pools->m_pVehicle = new CVehiclePool();
    pools->m_pPlayer = new CPlayerPool();
    pools->m_pPickup = new CPickupPool();
    pools->m_pObject = new CObjectPool();
    pools->m_pActor = new CActorPool();
    pools->m_pGangZone = new CGangZonePool();
    pools->m_pLabel = new CLabelPool();
    pools->m_pTextDraw = new CTextDrawPool();
    pools->m_pMenu = new CMenuPool();

    DestroyPools(m_pPools);
    m_pPools = pools;
}

// init default game settings
void CNetGame::InitializeGameLogic()
{
    Log("[CNetGame::InitializeGameLogic] initializing defaults");
    if (!m_pSettings)
        return;

    m_pSettings->m_bUseCJWalk = false;
    m_pSettings->m_nDeadDropsMoney = 0;
    m_pSettings->m_fWorldBoundaries[0] = 20000.0f;
    m_pSettings->m_fWorldBoundaries[1] = -20000.0f;
    m_pSettings->m_fWorldBoundaries[2] = 20000.0f;
    m_pSettings->m_fWorldBoundaries[3] = -20000.0f;
    m_pSettings->m_bAllowWeapons = true;
    m_pSettings->m_fGravity = 0.008f;
    m_pSettings->m_bEnterExit = true;
    m_pSettings->m_bVehicleFriendlyFire = FALSE;
    m_pSettings->m_bHoldTime = true;
    m_pSettings->m_bInstagib = false;
    m_pSettings->m_bZoneNames = false;
    m_pSettings->m_bFriendlyFire = true;
    m_pSettings->m_bClassesAvailable = FALSE;
    m_pSettings->m_fNameTagsDrawDist = 70.0f;
    m_pSettings->m_bManualVehicleEngineAndLight = false;
    m_pSettings->m_nWorldTimeHour = 12;
    m_pSettings->m_nWorldTimeMinute = 0;
    m_pSettings->m_nWeather = 10;
    m_pSettings->m_bNoNametagsBehindWalls = true;
    m_pSettings->m_nPlayerMarkersMode = MARKERS_MODE_GLOBAL;
    m_pSettings->m_fChatRadius = 200.0f;
    m_pSettings->m_bNameTags = true;
    m_pSettings->m_bLtdChatRadius = false;

    if (auto* game = g_game.get())
    {
        game->SetWorldGravity(m_pSettings->m_fGravity);
        game->EnableClock(false);
        game->EnableZoneDisplaying(false);
    }
}

// connect
void CNetGame::Connect()
{
    if (!m_pRakClient || !m_szHostAddress[0] || m_nPort < 1 || m_nPort > 65535)
        return;

    Log("[CNetGame::Connect()] host=%s, port=%d", m_szHostAddress, m_nPort);

    char message[320]{};
    sprintf_s(message, "Connecting to %s:%d...", m_szHostAddress, m_nPort);
    if (g_chat)
        g_chat->AddMessage(0xFFFFFFFFu, message);

    // connect to server
    const bool started = m_pRakClient->Connect(m_szHostAddress, static_cast<unsigned short>(m_nPort), 0, 0, 5);
    Log("[net:connect] started=%d, auth revision=3", started);

    m_lastConnectAttempt = GetTimeNow();
    SetState(started ? GAME_MODE_CONNECTING : GAME_MODE_WAITCONNECT); // set game state
}

// disconnect
void CNetGame::Disconnect()
{
    BuildingRemoval::RestoreAll();
    ResetObjectPool();
    ResetActorPool();
    if (m_pRakClient && m_pRakClient->IsConnected())
        m_pRakClient->Disconnect(500);
}

// spawn screen (class selection)
void CNetGame::SpawnScreen()
{
    // TODO: remove this wrapper
    Log("[CNetGame::SpawnScreen] class selection requested");
    if (auto* pool = GetPlayerPool())
        if (auto* player = pool->GetLocalPlayer())
            player->PrepareForClassSelection();
    render::ShowClassSelection(true);
}

// handle RSA public key mismatch
void CNetGame::Packet_RSAPublicKeyMismatch(Packet*)
{
    g_chat->PrintDebug("Server authentication key mismatch.");
    SetState(GAME_MODE_WAITCONNECT);
}

// connection banned
void CNetGame::Packet_ConnectionBanned(Packet*)
{
    g_chat->PrintDebug("Connection rejected by server.");
    SetState(GAME_MODE_WAITCONNECT);
}

// wait for join
void CNetGame::Packet_ConnectionRequestAcepted(Packet* packet)
{
    SetState(GAME_MODE_WAITJOIN);
}

// when server is full
void CNetGame::Packet_NoFreeIncomingConnections(Packet*)
{
    g_chat->PrintDebug("Server is full.");
    SetState(GAME_MODE_WAITCONNECT);
}

// disconnect notification
void CNetGame::Packet_DisconnectionNotification(Packet*)
{
    BuildingRemoval::RestoreAll();
    g_chat->PrintDebug("Server closed the connection.");
    CSrvNetStats::Reset();
    SetState(GAME_MODE_WAITCONNECT);
}

// when password is invalid
void CNetGame::Packet_InvalidPassword(Packet*)
{
    g_chat->PrintDebug("Invalid server password.");
    SetState(GAME_MODE_WAITCONNECT);
}

// when server is didn't respond
void CNetGame::Packet_ConnectionAttemptFailed(Packet*)
{
    g_chat->PrintDebug("Server did not respond. Retrying...");
    SetState(GAME_MODE_WAITCONNECT);
}

// player update
void CNetGame::UpdatePlayers()
{
    // wait for next update interval
    const std::uint32_t now = GetTimeNow();
    if (now - RefLastPlayersUpdateRequest() < NETMODE_PLAYERS_UPDATE_DELAY) // 3000
        return;

    // update timestamp
    RefLastPlayersUpdateRequest() = now;

    UpdateScoresAndPings();
}

// delete map icon
void CNetGame::DeleteMarker(std::uint8_t index)
{
    if (index >= 100 || !m_aMapIcons[index])
        return;

    if (auto* game = g_game.get())
        game->DeleteMarker(m_aMapIcons[index]);
    m_aMapIcons[index] = 0;
}

// reset player pool
void CNetGame::ResetPlayerPool()
{
    Log("[CNetGame::ResetPlayerPool]");
    if (!m_pPools)
        return;

    std::string name;
    if (m_pPools->m_pPlayer)
        name = m_pPools->m_pPlayer->GetLocalPlayerName();

    ResetPool(m_pPools->m_pPlayer);

    if (m_pPools->m_pPlayer)
        m_pPools->m_pPlayer->SetLocalPlayerName(name.c_str());
}

// reset vehicle pool
void CNetGame::ResetVehiclePool()
{
    Log("[CNetGame::ResetVehiclePool]");
    if (m_pPools)
    {
        ResetPool(m_pPools->m_pVehicle);
    }
}

// reset textdraw pool
void CNetGame::ResetTextDrawPool()
{
    Log("[CNetGame::ResetTextDrawPool]");
    if (g_textDrawSelection)
        g_textDrawSelection->Disable(false);
    if (m_pPools)
    {
        ResetPool(m_pPools->m_pTextDraw);
    }
}

// reset object pool
void CNetGame::ResetObjectPool()
{
    Log("[CNetGame::ResetObjectPool]");
    if (g_objectSelection)
        g_objectSelection->Enable(FALSE);
    if (g_objectEdit && g_objectEdit->m_bEnabled)
        g_objectEdit->Disable(FALSE, false);
    if (m_pPools)
    {
        ResetPool(m_pPools->m_pObject);
    }
}

// reset gang zone pool
void CNetGame::ResetGangZonePool()
{
    Log("[CNetGame::ResetGangZonePool]");
    if (m_pPools)
    {
        ResetPool(m_pPools->m_pGangZone);
    }
}

// reset pickup pool
void CNetGame::ResetPickupPool()
{
    Log("[CNetGame::ResetPickupPool]");
    if (m_pPools)
    {
        ResetPool(m_pPools->m_pPickup);
    }
}

// reset menu pool
void CNetGame::ResetMenuPool()
{
    Log("[CNetGame::ResetMenuPool]");
    if (m_pPools)
    {
        ResetPool(m_pPools->m_pMenu);
    }
}

// reset label pool
void CNetGame::ResetLabelPool()
{
    Log("[CNetGame::ResetLabelPool]");
    if (m_pPools)
    {
        ResetPool(m_pPools->m_pLabel);
    }
}

// reset actor pool
void CNetGame::ResetActorPool()
{
    Log("[CNetGame::ResetActorPool]");
    if (m_pPools)
    {
        ResetPool(m_pPools->m_pActor);
    }
}

// unoccupied sync
void CNetGame::Packet_UnoccupiedSync(Packet* packet)
{
    if (m_nGameState != GAME_MODE_CONNECTED || !packet || !packet->data)
        return;

    BitStream bs(packet->data, packet->length, false);
    std::uint32_t timestamp{};
    ID playerId{};
    Synchronization::UnoccupiedData data{};
    if (!ReadSyncHeader(bs, ID_UNOCCUPIED_SYNC, timestamp) || !bs.Read(playerId) || !ReadSyncBody(bs, data))
        return;

    if (auto* player = ResolveRemote(this, playerId); player && player->DoesExist())
        player->Update(&data);
}

// bullet sync
void CNetGame::Packet_BulletSync(Packet* packet)
{
    // needs the separate combat event implementation
    (void)packet;
}

// aim sync
void CNetGame::Packet_AimSync(Packet* packet)
{
    if (m_nGameState != GAME_MODE_CONNECTED || !packet || !packet->data)
        return;

    BitStream bs(packet->data, packet->length, false);
    std::uint32_t timestamp{};
    ID playerId{};
    Synchronization::AimData data{};
    if (!ReadSyncHeader(bs, ID_AIM_SYNC, timestamp) || !bs.Read(playerId) || !ReadSyncBody(bs, data))
        return;

    if (auto* player = ResolveRemote(this, playerId); player && player->DoesExist())
        player->Update(&data);
}

// passenger sync
void CNetGame::Packet_PassengerSync(Packet* packet)
{
    if (m_nGameState != GAME_MODE_CONNECTED || !packet || !packet->data)
        return;

    BitStream bs(packet->data, packet->length, false);
    std::uint32_t timestamp{};
    ID playerId{};
    Synchronization::PassengerData data{};
    if (!ReadSyncHeader(bs, ID_PASSENGER_SYNC, timestamp) || !bs.Read(playerId) || !ReadSyncBody(bs, data))
        return;

    if (auto* player = ResolveRemote(this, playerId); player && player->DoesExist())
        player->Update(&data, timestamp);
}

// trailer sync
void CNetGame::Packet_TrailerSync(Packet* packet)
{
    if (m_nGameState != GAME_MODE_CONNECTED || !packet || !packet->data)
        return;

    BitStream bs(packet->data, packet->length, false);
    std::uint32_t timestamp{};
    ID playerId{};
    Synchronization::TrailerData data{};
    if (!ReadSyncHeader(bs, ID_TRAILER_SYNC, timestamp) || !bs.Read(playerId) || !ReadSyncBody(bs, data))
        return;

    if (auto* player = ResolveRemote(this, playerId); player && player->DoesExist())
        player->Update(&data);
}
// markers sync
void CNetGame::Packet_MarkersSync(Packet* packet)
{
    if (m_nGameState != GAME_MODE_CONNECTED || !packet || !packet->data)
        return;

    BitStream bs(packet->data, packet->length, false);
    std::uint32_t timestamp{};
    std::uint32_t count{};
    if (!ReadSyncHeader(bs, ID_MARKERS_SYNC, timestamp) || !bs.Read(count) || count > CPlayerPool::MAX_PLAYERS || bs.GetNumberOfUnreadBits() < count * 17u)
        return;

    struct Entry
    {
        ID id{};
        bool active{};
        std::int16_t x{}, y{}, z{};
    };

    std::vector<Entry> entries;
    entries.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i)
    {
        Entry entry{};
        if (!bs.Read(entry.id) || !bs.Read(entry.active))
            return;
        if (entry.active && (!bs.Read(entry.x) || !bs.Read(entry.y) || !bs.Read(entry.z)))
            return;
        entries.push_back(entry);
    }

    auto* pool = GetPlayerPool();
    if (!pool)
        return;

    for (const Entry& entry : entries)
    {
        if (entry.id == pool->m_localInfo.m_nId)
            continue;
        auto* player = pool->GetPlayer(entry.id);
        if (!player)
            continue;
        if (entry.active)
            player->SetMarkerPosition(entry.x, entry.y, entry.z);
        player->SetMarkerState(entry.active ? TRUE : FALSE);
    }
}

// reset all markers
void CNetGame::ResetMarkers()
{
    for (std::uint8_t i = 0; i < 100; ++i)
        DeleteMarker(i);
}

// create marker
void CNetGame::CreateMarker(std::uint8_t index, CVector position, char icon, int color, int type)
{
    if (index >= 100)
        return;

    DeleteMarker(index);
    if (auto* game = g_game.get())
        m_aMapIcons[index] = game->CreateMarker(static_cast<int>(static_cast<unsigned char>(icon)), position, color, type);
}

// reset all pools
void CNetGame::ResetPools()
{
    Log("[CNetGame::ResetPools]");

    ResetPlayerPool();
    ResetVehiclePool();
    ResetTextDrawPool();
    ResetObjectPool();
    ResetGangZonePool();
    ResetPickupPool();
    ResetMenuPool();
    ResetLabelPool();
    ResetActorPool();
    ResetMarkers();
}

// shutdown for restart
void CNetGame::ShutdownForRestart()
{
    Log("[CNetGame::ShutdownForRestart] starting");
    if (!m_pSettings)
        return;

    m_pSettings->m_nWorldTimeHour = 12;
    m_pSettings->m_nWorldTimeMinute = 0;
    m_pSettings->m_nWeather = 10;
    m_pSettings->m_bHoldTime = true;
    m_pSettings->m_bUseCJWalk = false;
    m_pSettings->m_fGravity = 0.008f;
    m_pSettings->m_nDeadDropsMoney = 0;
    m_pSettings->m_bEnterExit = true;
    m_pSettings->m_fNameTagsDrawDist = 70.0f;
    m_pSettings->m_bZoneNames = false;
    m_pSettings->m_bNoNametagsBehindWalls = true;

    SetState(GAME_MODE_RESTARTING);

    if (auto* playerPool = GetPlayerPool())
    {
        if (auto* local = playerPool->GetLocalPlayer())
            local->EnableSpectating(FALSE);
        playerPool->Deactivate();
        playerPool->Process();
    }

    ResetVehiclePool();
    ResetTextDrawPool();
    ResetObjectPool();
    ResetGangZonePool();
    ResetPickupPool();
    ResetMenuPool();
    ResetLabelPool();
    ResetActorPool();
    ResetMarkers();
    PlayerColors::Initialize();

    if (auto* game = g_game.get())
    {
        game->SetWorldGravity(m_pSettings->m_fGravity);
        game->SetWantedLevel(0);
        game->EnableClock(false);
        game->EnableZoneDisplaying(false);
        game->ResetMoney();
    }
}

// player sync
void CNetGame::Packet_PlayerSync(Packet* packet)
{
    if (m_nGameState != GAME_MODE_CONNECTED || !packet || !packet->data)
        return;

    BitStream bs(packet->data, packet->length, false);
    std::uint32_t timestamp{};
    ID playerId{};
    Synchronization::OnfootData data{};
    if (!ReadSyncHeader(bs, ID_PLAYER_SYNC, timestamp) || !bs.Read(playerId) || !ReadOnfootData(bs, data))
        return;

    if (auto* player = ResolveRemote(this, playerId); player && player->DoesExist())
        player->Update(&data, timestamp);
}

// vehicle sync
void CNetGame::Packet_VehicleSync(Packet* packet)
{
    if (m_nGameState != GAME_MODE_CONNECTED || !packet || !packet->data)
        return;

    BitStream bs(packet->data, packet->length, false);
    std::uint32_t timestamp{};
    ID playerId{};
    Synchronization::IncarData data{};
    if (!ReadSyncHeader(bs, ID_VEHICLE_SYNC, timestamp) || !bs.Read(playerId) || !ReadIncarData(bs, data))
        return;

    if (auto* player = ResolveRemote(this, playerId); player && player->DoesExist())
        player->Update(&data, timestamp);
}

// when connection lost
void CNetGame::Packet_ConnectionLost(Packet*)
{
    BuildingRemoval::RestoreAll();
    g_chat->PrintDebug("Connection to the server was lost. Reconnecting...");
    CSrvNetStats::Reset();
    SetState(GAME_MODE_WAITCONNECT);
}

// update network (network loop)
void CNetGame::UpdateNetwork()
{
    if (!m_pRakClient)
        return;
    while (Packet* packet = m_pRakClient->Receive())
    {
        if (!packet->data || packet->length == 0)
        {
            m_pRakClient->DeallocatePacket(packet);
            continue;
        }
        unsigned char packetId = packet->data[0];

        // timestamped packet
        if (packetId == ID_TIMESTAMP)
        {
            if (packet->length <= 1 + sizeof(unsigned long))
            {
                m_pRakClient->DeallocatePacket(packet);
                continue;
            }
            packetId = packet->data[1 + sizeof(unsigned long)];
        }

        if (packetId != ID_PLAYER_SYNC && packetId != ID_VEHICLE_SYNC && packetId != ID_PASSENGER_SYNC && packetId != ID_AIM_SYNC && packetId != ID_TRAILER_SYNC &&
            packetId != ID_UNOCCUPIED_SYNC)
            Log("[net:receive] id=%u, length=%u", static_cast<unsigned>(packetId), packet->length);
        switch (packetId)
        {
            case ID_RSA_PUBLIC_KEY_MISMATCH:
                Packet_RSAPublicKeyMismatch(packet);
                break;

            case ID_CONNECTION_BANNED:
                Packet_ConnectionBanned(packet);
                break;

            case ID_CONNECTION_REQUEST_ACCEPTED:
                Packet_ConnectionSucceeded(packet);
                break;

            case ID_NO_FREE_INCOMING_CONNECTIONS:
                Packet_NoFreeIncomingConnections(packet);
                break;

            case ID_DISCONNECTION_NOTIFICATION:
                Packet_DisconnectionNotification(packet);
                break;

            case ID_INVALID_PASSWORD:
                Packet_InvalidPassword(packet);
                break;

            case ID_CONNECTION_ATTEMPT_FAILED:
                Packet_ConnectionAttemptFailed(packet);
                break;

            case ID_CONNECTION_LOST:
                Packet_ConnectionLost(packet);
                break;

            case ID_UNOCCUPIED_SYNC:
                Packet_UnoccupiedSync(packet);
                break;

            case ID_BULLET_SYNC:
                Packet_BulletSync(packet);
                break;

            case ID_AIM_SYNC:
                Packet_AimSync(packet);
                break;

            case ID_PASSENGER_SYNC:
                Packet_PassengerSync(packet);
                break;

            case ID_TRAILER_SYNC:
                Packet_TrailerSync(packet);
                break;

            case ID_MARKERS_SYNC:
                Packet_MarkersSync(packet);
                break;

            case ID_AUTH_KEY:
                Packet_AuthKey(packet);
                break;

            case ID_PLAYER_SYNC:
                Packet_PlayerSync(packet);
                break;

            case ID_VEHICLE_SYNC:
                Packet_VehicleSync(packet);
                break;

            default:
                break;
        }

        m_pRakClient->DeallocatePacket(packet);
    }
}

// process network
void CNetGame::Process()
{
    UpdateNetwork();

    const std::uint32_t now = GetTimeNow();
    if (m_nGameState == GAME_MODE_WAITCONNECT && now - m_lastConnectAttempt > 3000)
        Connect();

    if (CGame::IsPaused())
        return;

    if (m_nGameState == GAME_MODE_CONNECTED)
        ProcessGameStuff();

    if (!m_pPools)
        return;

    if (m_pPools->m_pPlayer)
        m_pPools->m_pPlayer->Process();

    if (m_pPools->m_pPickup)
        m_pPools->m_pPickup->Process();

    if (m_pPools->m_pObject)
        m_pPools->m_pObject->Process();

    if (m_pPools->m_pVehicle)
    {
        m_pPools->m_pVehicle->ProcessWaitingList();
        m_pPools->m_pVehicle->Process();
    }

    if (m_nGameState == GAME_MODE_CONNECTED)
        UpdatePlayers();
}

// process game stuff
void CNetGame::ProcessGameStuff()
{
    if (!m_pSettings)
        return;

    auto* game = g_game.get();
    if (!game)
        return;

    if (m_pSettings->m_bHoldTime)
        game->SetWorldTime(static_cast<char>(m_pSettings->m_nWorldTimeHour), static_cast<char>(m_pSettings->m_nWorldTimeMinute));

    game->SetWorldWeather(static_cast<char>(m_pSettings->m_nWeather));
    game->SetWorldGravity(m_pSettings->m_fGravity);
    game->EnableZoneDisplaying(m_pSettings->m_bZoneNames);
}

// returns player pool
CPlayerPool* CNetGame::GetPlayerPool()
{
    return m_pPools ? m_pPools->m_pPlayer : nullptr;
}

// returns object pool
CObjectPool* CNetGame::GetObjectPool()
{
    return m_pPools ? m_pPools->m_pObject : nullptr;
}

// returns actor pool
CActorPool* CNetGame::GetActorPool()
{
    return m_pPools ? m_pPools->m_pActor : nullptr;
}

// returns vehicle pool
CVehiclePool* CNetGame::GetVehiclePool()
{
    return m_pPools ? m_pPools->m_pVehicle : nullptr;
}

// returns pickup pool
CPickupPool* CNetGame::GetPickupPool()
{
    return m_pPools ? m_pPools->m_pPickup : nullptr;
}

// returns menu pool
CMenuPool* CNetGame::GetMenuPool()
{
    return m_pPools ? m_pPools->m_pMenu : nullptr;
}

int CNetGame::GetState()
{
    return m_nGameState;
}

BOOL CNetGame::LanMode()
{
    return m_bLanMode;
}

RakClientInterface* CNetGame::GetRakClient()
{
    return m_pRakClient;
}

__int64 CNetGame::GetCounter()
{
    LARGE_INTEGER counter{};
    ::QueryPerformanceCounter(&counter);
    return counter.QuadPart;
}

// --------------------------------------------------

void CNetGame::SendRconCommand(const char* command)
{
    if (!m_pRakClient || !command)
        return;

    const auto length = static_cast<std::uint32_t>(std::strlen(command));

    BitStream output;
    output.Write(static_cast<std::uint8_t>(ID_RCON_COMMAND));
    output.Write(length);

    if (length != 0)
        output.Write(command, static_cast<int>(length));

    m_pRakClient->Send(&output, HIGH_PRIORITY, RELIABLE, 0);
}

// handle auth key
void CNetGame::Packet_AuthKey(Packet* packet)
{
    if (!m_pRakClient || !packet || !packet->data || packet->length < 2)
        return;
    BitStream input(packet->data, packet->length, false);
    std::uint8_t id = 0;
    if (!input.Read(id))
        return;
    if (id == ID_TIMESTAMP)
    {
        std::uint32_t timestamp = 0;
        if (!input.Read(timestamp) || !input.Read(id))
            return;
    }
    std::uint8_t length = 0;
    if (id != ID_AUTH_KEY || !input.Read(length) || !length || input.GetNumberOfUnreadBits() < length * 8)
        return;
    std::string challenge(length, '\0');
    if (!input.Read(challenge.data(), length))
        return;
    Log("[authentication] challenge=%s, wire length=%u, last byte=%02X", challenge.c_str(), static_cast<unsigned>(length), static_cast<unsigned char>(challenge.back()));
    const auto key = authentication::make_auth_key(g_archive, challenge);
    if (!key)
    {
        Log("[authentication] failed to generate auth key");
        return;
    }
    Log("[authentication] response=%s", key->c_str());
    BitStream response;
    response.Write(static_cast<std::uint8_t>(ID_AUTH_KEY));
    response.Write(static_cast<std::uint8_t>(key->size()));
    response.Write(key->data(), static_cast<int>(key->size()));
    const bool sent = m_pRakClient->Send(&response, SYSTEM_PRIORITY, RELIABLE, 0);
    Log("[authentication] response sent=%d, length=%u", sent, static_cast<unsigned>(key->size()));
}

// --------------------------------------------------

// when connection succeeded
void CNetGame::Packet_ConnectionSucceeded(Packet* packet)
{
    if (!packet || !packet->data || !m_pRakClient || !GetPlayerPool())
        return;

    CSrvNetStats::Reset();

    BitStream input(packet->data, packet->length, false);
    std::uint8_t id = 0;
    std::uint32_t address = 0, challenge = 0;
    std::uint16_t port = 0, playerId = 0;
    if (!input.Read(id) || id != ID_CONNECTION_REQUEST_ACCEPTED || !input.Read(address) || !input.Read(port) || !input.Read(playerId) || !input.Read(challenge))
        return;

    const std::string nick = GetPlayerPool()->GetLocalPlayerName();
    const std::string userDirectory = GetSampUserDirectory();
    if (userDirectory.empty() || nick.empty() || nick.size() > 24)
    {
        Log("[net:join] invalid nickname or user directory");
        return;
    }
    const std::string gpci = authentication::make_gpci(userDirectory);
    const auto response = authentication::make_challenge_response(challenge);
    const auto version = authentication::client_version_string();
    GetPlayerPool()->m_localInfo.m_nId = playerId;
    SetState(GAME_MODE_WAITJOIN);
    const bool sent = ClientJoin(authentication::client_version(), 1, nick.c_str(), response, gpci.c_str(), version.data());
    Log("[net:join] sent=%d, player=%u, nick=%s, version=%.*s, gpci=%s", sent, playerId, nick.c_str(), static_cast<int>(version.size()), version.data(), gpci.c_str());
}

// --------------------------------------------------

// EnterVehicle
bool CNetGame::EnterVehicle(std::uint16_t vehicleId, std::uint8_t isPassenger)
{
    if (!m_pRakClient || !m_pRakClient->IsConnected() || m_nGameState != GAME_MODE_CONNECTED)
        return false;
    BitStream bs;
    bs.Write(vehicleId);
    bs.Write(static_cast<std::uint8_t>(isPassenger));
    return m_pRakClient->RPC(&g_EnterVehicle, &bs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, false);
}

// ExitVehicle
bool CNetGame::ExitVehicle(std::uint16_t vehicleId)
{
    if (!m_pRakClient || !m_pRakClient->IsConnected() || m_nGameState != GAME_MODE_CONNECTED)
        return false;
    BitStream bs;
    bs.Write(vehicleId);
    return m_pRakClient->RPC(&g_ExitVehicle, &bs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, false);
}

// VehicleDamaged
bool CNetGame::VehicleDamaged(std::uint16_t vehicleId, std::uint32_t panels, std::uint32_t doors, std::uint8_t lights, std::uint8_t tires)
{
    if (!m_pRakClient || !m_pRakClient->IsConnected() || m_nGameState != GAME_MODE_CONNECTED)
        return false;
    BitStream bs;
    bs.Write(vehicleId);
    bs.Write(panels);
    bs.Write(doors);
    bs.Write(lights);
    bs.Write(tires);
    return m_pRakClient->RPC(&g_VehicleDamaged, &bs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, false);
}

// ScmEvent
bool CNetGame::ScmEvent(std::uint32_t vehicleId, std::uint32_t param1, std::uint32_t param2, std::uint32_t eventType)
{
    if (!m_pRakClient || !m_pRakClient->IsConnected() || m_nGameState != GAME_MODE_CONNECTED)
        return false;
    BitStream bs;
    bs.Write(vehicleId);
    bs.Write(param1);
    bs.Write(param2);
    bs.Write(eventType);
    return m_pRakClient->RPC(&g_ScmEvent, &bs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, false);
}

// VehicleDestroyed
bool CNetGame::VehicleDestroyed(std::uint16_t vehicleId)
{
    if (!m_pRakClient || !m_pRakClient->IsConnected() || m_nGameState != GAME_MODE_CONNECTED)
        return false;
    BitStream bs;
    bs.Write(vehicleId);
    return m_pRakClient->RPC(&g_VehicleDestroyed, &bs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, false);
}

// SendSpawn
bool CNetGame::SendSpawn()
{
    if (!m_pRakClient || !m_pRakClient->IsConnected() || m_nGameState != GAME_MODE_CONNECTED)
        return false;

    BitStream output;
    return m_pRakClient->RPC(&g_SendSpawn, &output, HIGH_PRIORITY, RELIABLE_SEQUENCED, 0, false);
}

// ChatMessage
bool CNetGame::ChatMessage(const char* message)
{
    if (!m_pRakClient || !m_pRakClient->IsConnected() || m_nGameState != GAME_MODE_CONNECTED)
        return false;
    if (!message)
        return false;
    const auto length = std::strlen(message);
    if (length > 255)
        return false;
    BitStream bs;
    bs.Write(static_cast<std::uint8_t>(length));
    if (length)
        bs.Write(message, static_cast<int>(length));
    return m_pRakClient->RPC(&g_ChatMessage, &bs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, false);
}

// InteriorChangeNotification
bool CNetGame::InteriorChangeNotification(std::uint8_t interiorId)
{
    if (!m_pRakClient || !m_pRakClient->IsConnected() || m_nGameState != GAME_MODE_CONNECTED)
        return false;
    BitStream bs;
    bs.Write(interiorId);
    return m_pRakClient->RPC(&g_InteriorChangeNotification, &bs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, false);
}

// DeathNotification
bool CNetGame::DeathNotification(std::uint8_t reason, std::uint16_t killerId)
{
    if (!m_pRakClient || !m_pRakClient->IsConnected() || m_nGameState != GAME_MODE_CONNECTED)
        return false;
    BitStream bs;
    bs.Write(reason);
    bs.Write(killerId);
    return m_pRakClient->RPC(&g_DeathNotification, &bs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, false);
}

// SendCommand
bool CNetGame::SendCommand(const char* text)
{
    Log("SendCommand");

    if (!m_pRakClient || !text)
        return false;

    const auto length = static_cast<std::uint32_t>(std::strlen(text));

    BitStream bs;

    bs.Write(length);

    bs.Write(text, static_cast<int>(length));

    return m_pRakClient->RPC(&g_SendCommand, &bs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, false);
}

// ClickPlayer
bool CNetGame::ClickPlayer(std::uint16_t playerId, std::uint8_t source)
{
    BitStream bs;

    bs.Write(playerId);
    bs.Write(source);

    return m_pRakClient->RPC(&g_ClickPlayer, &bs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, false);
}

// DialogResponse
bool CNetGame::DialogResponse(std::uint16_t dialogId, bool response, std::int16_t listItem, const char* inputText)
{
    if (!m_pRakClient || !m_pRakClient->IsConnected() || m_nGameState != GAME_MODE_CONNECTED)
        return false;

    const char* text = inputText ? inputText : "";
    const auto length = static_cast<std::uint8_t>(std::min<std::size_t>(std::strlen(text), 255));

    BitStream bs;
    bs.Write(dialogId);
    bs.Write(static_cast<std::uint8_t>(response));
    bs.Write(static_cast<std::uint16_t>(listItem));
    bs.Write(length);
    bs.Write(text, static_cast<int>(length));

    return m_pRakClient->RPC(&g_DialogResponse, &bs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, false);
}

// ClientCheckResponse
bool CNetGame::ClientCheckResponse(std::uint8_t type, std::uint32_t address, std::uint8_t response)
{
    if (!m_pRakClient || !m_pRakClient->IsConnected() || m_nGameState != GAME_MODE_CONNECTED)
        return false;
    BitStream bs;
    bs.Write(type);
    bs.Write(address);
    bs.Write(response);
    return m_pRakClient->RPC(&g_ClientCheckResponse, &bs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, false);
}

// GiveTakeDamage
bool CNetGame::GiveTakeDamage(bool giveOrTake, std::uint16_t playerId, float damage, std::uint32_t weaponId, std::uint32_t bodyPart)
{
    if (!m_pRakClient || !m_pRakClient->IsConnected() || m_nGameState != GAME_MODE_CONNECTED)
        return false;
    BitStream bs;
    bs.Write(giveOrTake);
    bs.Write(playerId);
    bs.Write(damage);
    bs.Write(weaponId);
    bs.Write(bodyPart);
    return m_pRakClient->RPC(&g_GiveTakeDamage, &bs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, false);
}

// GiveActorDamage
bool CNetGame::GiveActorDamage(bool unused, std::uint16_t actorId, float damage, std::uint32_t weaponId, std::uint32_t bodyPart)
{
    if (!m_pRakClient || !m_pRakClient->IsConnected() || m_nGameState != GAME_MODE_CONNECTED)
        return false;
    BitStream bs;
    bs.Write(unused);
    bs.Write(actorId);
    bs.Write(damage);
    bs.Write(weaponId);
    bs.Write(bodyPart);
    return m_pRakClient->RPC(&g_GiveActorDamage, &bs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, false);
}

// MapMarker
bool CNetGame::MapMarker(float x, float y, float z)
{
    if (!m_pRakClient || !m_pRakClient->IsConnected() || m_nGameState != GAME_MODE_CONNECTED)
        return false;
    BitStream bs;
    bs.Write(x);
    bs.Write(y);
    bs.Write(z);
    return m_pRakClient->RPC(&g_MapMarker, &bs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, false);
}

// RequestClass
bool CNetGame::RequestClass(std::uint16_t classId)
{
    if (!m_pRakClient || !m_pRakClient->IsConnected() || m_nGameState != GAME_MODE_CONNECTED)
        return false;

    BitStream bs;
    bs.Write(classId);
    return m_pRakClient->RPC(&g_RequestClass, &bs, HIGH_PRIORITY, RELIABLE, 0, false);
}

// RequestSpawn
bool CNetGame::RequestSpawn()
{
    if (!m_pRakClient || !m_pRakClient->IsConnected() || m_nGameState != GAME_MODE_CONNECTED)
        return false;

    BitStream output;
    return m_pRakClient->RPC(&g_RequestSpawn, &output, HIGH_PRIORITY, RELIABLE, 0, false);
}

// MenuSelect
bool CNetGame::MenuSelect(std::uint8_t row)
{
    if (!m_pRakClient || !m_pRakClient->IsConnected() || m_nGameState != GAME_MODE_CONNECTED)
        return false;
    BitStream bs;
    bs.Write(row);
    return m_pRakClient->RPC(&g_MenuSelect, &bs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, false);
}

// MenuQuit
bool CNetGame::MenuQuit()
{
    if (!m_pRakClient || !m_pRakClient->IsConnected() || m_nGameState != GAME_MODE_CONNECTED)
        return false;
    BitStream bs;

    return m_pRakClient->RPC(&g_MenuQuit, &bs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, false);
}

// SelectTextDraw
bool CNetGame::SelectTextDraw(std::uint16_t textDrawId)
{
    if (!m_pRakClient || !m_pRakClient->IsConnected() || m_nGameState != GAME_MODE_CONNECTED)
        return false;
    BitStream bs;
    bs.Write(textDrawId);
    return m_pRakClient->RPC(&g_SelectTextDraw, &bs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, false);
}

// PickedUpPickup
bool CNetGame::PickedUpPickup(std::int32_t pickupId, PickupSendMode mode)
{
    if (!m_pRakClient || !m_pRakClient->IsConnected() || m_nGameState != GAME_MODE_CONNECTED)
        return false;

    BitStream bs;
    bs.Write(pickupId);
    const auto reliability = mode == PickupSendMode::Notification ? RELIABLE_ORDERED : RELIABLE_SEQUENCED;
    return m_pRakClient->RPC(&g_PickedUpPickup, &bs, HIGH_PRIORITY, reliability, 0, false);
}

// SelectObject
bool CNetGame::SelectObject(std::uint32_t selectType, std::uint16_t objectId, std::uint32_t modelId, float x, float y, float z)
{
    if (!m_pRakClient || !m_pRakClient->IsConnected() || m_nGameState != GAME_MODE_CONNECTED)
        return false;
    BitStream bs;
    bs.Write(selectType);
    bs.Write(objectId);
    bs.Write(modelId);
    bs.Write(x);
    bs.Write(y);
    bs.Write(z);
    return m_pRakClient->RPC(&g_SelectObject, &bs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, false);
}

// EditAttachedObject
bool CNetGame::EditAttachedObject(std::uint32_t response, std::uint32_t index, std::uint32_t modelId, std::uint32_t boneId, const CVector& offset, const CVector& rotation,
                                  const CVector& scale, std::uint32_t color1, std::uint32_t color2)
{
    if (!m_pRakClient || !m_pRakClient->IsConnected() || m_nGameState != GAME_MODE_CONNECTED)
        return false;
    BitStream bs;
    bs.Write(response);
    bs.Write(index);
    bs.Write(modelId);
    bs.Write(boneId);
    bs.Write(offset.x);
    bs.Write(offset.y);
    bs.Write(offset.z);
    bs.Write(rotation.x);
    bs.Write(rotation.y);
    bs.Write(rotation.z);
    bs.Write(scale.x);
    bs.Write(scale.y);
    bs.Write(scale.z);
    bs.Write(color1);
    bs.Write(color2);
    return m_pRakClient->RPC(&g_EditAttachedObject, &bs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, false);
}

// EditObject
bool CNetGame::EditObject(bool playerObject, std::uint16_t objectId, std::uint32_t response, const CVector& position, const CVector& rotation)
{
    if (!m_pRakClient || !m_pRakClient->IsConnected() || m_nGameState != GAME_MODE_CONNECTED)
        return false;
    BitStream bs;
    bs.Write(playerObject);
    bs.Write(objectId);
    bs.Write(response);
    bs.Write(position.x);
    bs.Write(position.y);
    bs.Write(position.z);
    bs.Write(rotation.x);
    bs.Write(rotation.y);
    bs.Write(rotation.z);
    return m_pRakClient->RPC(&g_EditObject, &bs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, false);
}

// UpdateScoresAndPings
bool CNetGame::UpdateScoresAndPings()
{
    if (!m_pRakClient || !m_pRakClient->IsConnected() || m_nGameState != GAME_MODE_CONNECTED)
        return false;

    BitStream bs;
    return m_pRakClient->RPC(&g_UpdateScoresAndPings, &bs, HIGH_PRIORITY, RELIABLE, 0, false);
}

// ClientJoin
bool CNetGame::ClientJoin(std::uint32_t version, std::uint8_t mod, const char* nickname, std::uint32_t challengeResponse, const char* authKey, const char* clientVersion)
{
    if (!m_pRakClient || !m_pRakClient->IsConnected() || !nickname || !authKey || !clientVersion)
        return false;
    const auto nickLen = std::strlen(nickname);
    const auto authLen = std::strlen(authKey);
    const auto versionLen = std::strlen(clientVersion);
    if (nickLen > 24 || authLen > 255 || versionLen > 255)
        return false;

    BitStream bs;
    bs.Write(version);
    bs.Write(mod);
    bs.Write(static_cast<std::uint8_t>(nickLen));
    bs.Write(nickname, static_cast<int>(nickLen));
    bs.Write(challengeResponse);
    bs.Write(static_cast<std::uint8_t>(authLen));
    bs.Write(authKey, static_cast<int>(authLen));
    bs.Write(static_cast<std::uint8_t>(versionLen));
    bs.Write(clientVersion, static_cast<int>(versionLen));
    if (!g_DLClient)
        bs.Write(challengeResponse);
    return m_pRakClient->RPC(&g_ClientJoin, &bs, HIGH_PRIORITY, RELIABLE, 0, false);
}

/*
// NPCJoin - RPC 54 (bot clients only)
bool CNetGame::NPCJoin(std::uint32_t version, std::uint8_t mod, const char* nickname, std::uint32_t challengeResponse)
{
}
*/

// CameraTarget
bool CNetGame::CameraTarget(std::uint16_t objectTarget, std::uint16_t vehicleTarget, std::uint16_t playerTarget, std::uint16_t actorTarget)
{
    if (!m_pRakClient || !m_pRakClient->IsConnected() || m_nGameState != GAME_MODE_CONNECTED)
        return false;
    BitStream bs;
    bs.Write(objectTarget);
    bs.Write(vehicleTarget);
    bs.Write(playerTarget);
    bs.Write(actorTarget);
    return m_pRakClient->RPC(&g_CameraTarget, &bs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, false);
}

// SrvNetStats
// legacy
bool CNetGame::SrvNetStats()
{
    if (!m_pRakClient || !m_pRakClient->IsConnected() || m_nGameState != GAME_MODE_CONNECTED)
        return false;

    BitStream bs;
    return m_pRakClient->RPC(&g_SrvNetStats, &bs, HIGH_PRIORITY, RELIABLE, 0, false);
}

// DestroyWeaponPickup
// legacy
bool CNetGame::DestroyWeaponPickup(std::uint16_t exOwner)
{
    if (!m_pRakClient || !m_pRakClient->IsConnected() || m_nGameState != GAME_MODE_CONNECTED)
        return false;

    BitStream bs;
    bs.Write(static_cast<std::uint8_t>(exOwner));
    return m_pRakClient->RPC(&g_DestroyWeaponPickup, &bs, HIGH_PRIORITY, RELIABLE_SEQUENCED, 0, false);
}

// 0.3.DL RPC 181: request a DFF by CRC.
bool CNetGame::RequestDffFile(std::uint32_t crc)
{
    if (!g_DLClient || !m_pRakClient || !m_pRakClient->IsConnected() || m_nGameState != GAME_MODE_CONNECTED)
        return false;
    BitStream bs;
    bs.Write(crc);
    return m_pRakClient->RPC(&g_RequestDffFile, &bs, HIGH_PRIORITY, RELIABLE, 0, false);
}

// 0.3.DL RPC 182: request a TXD by CRC.
bool CNetGame::RequestTxdFile(std::uint32_t crc)
{
    if (!g_DLClient || !m_pRakClient || !m_pRakClient->IsConnected() || m_nGameState != GAME_MODE_CONNECTED)
        return false;
    BitStream bs;
    bs.Write(crc);
    return m_pRakClient->RPC(&g_RequestTxdFile, &bs, HIGH_PRIORITY, RELIABLE, 0, false);
}

// 0.3.DL RPC 184: acknowledge finished downloading.
bool CNetGame::FinishedDownloading(std::uint8_t unused)
{
    if (!g_DLClient || !m_pRakClient || !m_pRakClient->IsConnected() || m_nGameState != GAME_MODE_CONNECTED)
        return false;
    BitStream bs;
    bs.Write(unused);
    return m_pRakClient->RPC(&g_FinishedDownloading, &bs, HIGH_PRIORITY, RELIABLE, 0, false);
}
