/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * The original code is copyright (c) 2026, 1therealcloud.
 */

#pragma once

struct RPCParameters;

namespace RPCHandlers
{
    // RegisterRPCs1
    // SetPlayerSkillLevel - ID: 34
    void SetPlayerSkillLevel(RPCParameters* pParams);
    // Create3DTextLabel - ID: 36
    void Create3DTextLabel(RPCParameters* pParams);
    // Delete3DTextLabel - ID: 58
    void Delete3DTextLabel(RPCParameters* pParams);
    // ChatBubble - ID: 59
    void ChatBubble(RPCParameters* pParams);
    // ShowDialog - ID: 61
    void ShowDialog(RPCParameters* pParams);
    // SetCheckpoint - ID: 107
    void SetCheckpoint(RPCParameters* pParams);
    // DisableCheckpoint - ID: 37
    void DisableCheckpoint(RPCParameters* pParams);
    // SetRaceCheckpoint - ID: 38
    void SetRaceCheckpoint(RPCParameters* pParams);
    // DisableRaceCheckpoint - ID: 39
    void DisableRaceCheckpoint(RPCParameters* pParams);
    // UpdateScoresAndPings - ID: 155
    void UpdateScoresAndPings(RPCParameters* pParams);
    // Not listed in samp-packet-list wiki.
    void SrvNetStats(RPCParameters* pParams);
    // GameModeRestart - ID: 40
    void GameModeRestart(RPCParameters* pParams);
    // ConnectionRejected - ID: 130
    void ConnectionRejected(RPCParameters* pParams);
    // SendClientMessage - ID: 93
    void SendClientMessage(RPCParameters* pParams);
    // SetWorldTime - ID: 94
    void SetWorldTime(RPCParameters* pParams);
    // CreatePickup - ID: 95
    void CreatePickup(RPCParameters* pParams);
    // DestroyPickup - ID: 63
    void DestroyPickup(RPCParameters* pParams);
    // Not listed in samp-packet-list wiki.
    void DestroyWeaponPickup(RPCParameters* pParams); // legacy
    // ScmEvent - ID: 96
    void ScmEvent(RPCParameters* pParams);
    // SetWeather - ID: 152
    void SetWeather(RPCParameters* pParams);
    // SetPlayerTime - ID: 29
    void SetPlayerTime(RPCParameters* pParams);
    // ToggleClock - ID: 30
    void ToggleClock(RPCParameters* pParams);
    // SendGameTimeUpdate - ID: 60
    void SendGameTimeUpdate(RPCParameters* pParams);
    // WorldPlayerAdd - ID: 32
    void WorldPlayerAdd(RPCParameters* pParams);
    // DeathBroadcast - ID: 166
    void DeathBroadcast(RPCParameters* pParams);
    // WorldPlayerRemove - ID: 163
    void WorldPlayerRemove(RPCParameters* pParams);
    // WorldVehicleAdd - ID: 164
    void WorldVehicleAdd(RPCParameters* pParams);
    // WorldVehicleRemove - ID: 165
    void WorldVehicleRemove(RPCParameters* pParams);
    // UpdateVehicleDamageStatus - ID: 106
    void UpdateVehicleDamageStatus(RPCParameters* pParams);
    // SetVehicleParamsEx - ID: 24
    void SetVehicleParamsEx(RPCParameters* pParams);
    // PlayerEnterVehicle - ID: 26
    void PlayerEnterVehicle(RPCParameters* pParams);
    // PlayerExitVehicle - ID: 154
    void PlayerExitVehicle(RPCParameters* pParams);
    // ServerJoin - ID: 137
    void ServerJoin(RPCParameters* pParams);
    // ServerQuit - ID: 138
    void ServerQuit(RPCParameters* pParams);
    // InitGame - ID: 139
    void InitGame(RPCParameters* pParams);
    // ChatMessage - ID: 101 (wiki lists incoming RPC)
    void ChatMessage(RPCParameters* pParams);
    // RequestClass - ID: 128
    void RequestClass(RPCParameters* pParams);
    // RequestSpawn - ID: 129
    void RequestSpawn(RPCParameters* pParams);
    // EditAttachedObject - ID: 116
    void EditAttachedObject(RPCParameters* pParams);
    // EditObject - ID: 117
    void EditObject(RPCParameters* pParams);
    // EnterEditObject - ID: 27
    void EnterEditObject(RPCParameters* pParams);
    // CancelEdit - ID: 28
    void CancelEdit(RPCParameters* pParams);
    // ToggleCameraTarget - ID: 170
    void ToggleCameraTarget(RPCParameters* pParams);
    // ClientCheck - ID: 103
    void ClientCheck(RPCParameters* pParams);
    // ShowActor - ID: 171
    void ShowActor(RPCParameters* pParams);
    // HideActor - ID: 172
    void HideActor(RPCParameters* pParams);

    // RegisterRPCs2
    // DisableVehicleCollisions - ID: 167
    void DisableVehicleCollisions(RPCParameters* pParams);
    // SetMapIcon - ID: 56
    void SetMapIcon(RPCParameters* pParams);
    // RemoveMapIcon - ID: 144
    void RemoveMapIcon(RPCParameters* pParams);
    // SetWeaponAmmo - ID: 145
    void SetWeaponAmmo(RPCParameters* pParams);
    // SetGravity - ID: 146
    void SetGravity(RPCParameters* pParams);
    // SetVehicleHealth - ID: 147
    void SetVehicleHealth(RPCParameters* pParams);
    // AttachTrailerToVehicle - ID: 148
    void AttachTrailerToVehicle(RPCParameters* pParams);
    // DetachTrailerFromVehicle - ID: 149
    void DetachTrailerFromVehicle(RPCParameters* pParams);
    // CreateObject - ID: 44
    void CreateObject(RPCParameters* pParams);
    // SetObjectPos - ID: 45
    void SetObjectPos(RPCParameters* pParams);
    // SetObjectRotation - ID: 46
    void SetObjectRotation(RPCParameters* pParams);
    // DestroyObject - ID: 47
    void DestroyObject(RPCParameters* pParams);
    // CreateExplosion - ID: 79
    void CreateExplosion(RPCParameters* pParams);
    // ShowPlayerNameTag - ID: 80
    void ShowPlayerNameTag(RPCParameters* pParams);
    // MoveObject - ID: 99
    void MoveObject(RPCParameters* pParams);
    // StopObject - ID: 122
    void StopObject(RPCParameters* pParams);
    // SetVehicleTireStatus - ID: 98
    void SetVehicleTireStatus(RPCParameters* pParams);
    // SetVehicleNumberPlate - ID: 123
    void SetVehicleNumberPlate(RPCParameters* pParams);
    // TogglePlayerSpectating - ID: 124
    void TogglePlayerSpectating(RPCParameters* pParams);
    // SpectatePlayer - ID: 126
    void SpectatePlayer(RPCParameters* pParams);
    // SpectateVehicle - ID: 127
    void SpectateVehicle(RPCParameters* pParams);
    // RemoveVehicleComponent - ID: 57
    void RemoveVehicleComponent(RPCParameters* pParams);
    // ForceClassSelection - ID: 74
    void ForceClassSelection(RPCParameters* pParams);
    // AttachObjectToPlayer - ID: 75
    void AttachObjectToPlayer(RPCParameters* pParams);
    // SetPlayerWantedLevel - ID: 133
    void SetPlayerWantedLevel(RPCParameters* pParams);
    // ShowTextDraw - ID: 134
    void ShowTextDraw(RPCParameters* pParams);
    // HideTextDraw - ID: 135
    void HideTextDraw(RPCParameters* pParams);
    // TextDrawSetString - ID: 105
    void TextDrawSetString(RPCParameters* pParams);
    // AddGangZone - ID: 108
    void AddGangZone(RPCParameters* pParams);
    // GangZoneDestroy - ID: 120
    void GangZoneDestroy(RPCParameters* pParams);
    // GangZoneFlash - ID: 121
    void GangZoneFlash(RPCParameters* pParams);
    // GangZoneStopFlash - ID: 85
    void GangZoneStopFlash(RPCParameters* pParams);
    // ApplyPlayerAnimation - ID: 86
    void ApplyPlayerAnimation(RPCParameters* pParams);
    // ClearPlayerAnimation - ID: 87
    void ClearPlayerAnimation(RPCParameters* pParams);
    // SetPlayerSpecialAction - ID: 88
    void SetPlayerSpecialAction(RPCParameters* pParams);
    // EnableStuntBonus - ID: 104
    void EnableStuntBonus(RPCParameters* pParams);
    // SetPlayerFightingStyle - ID: 89
    void SetPlayerFightingStyle(RPCParameters* pParams);
    // SetPlayerVelocity - ID: 90
    void SetPlayerVelocity(RPCParameters* pParams);
    // SetVehicleVelocity - ID: 91
    void SetVehicleVelocity(RPCParameters* pParams);
    // ToggleWidescreen - ID: 111
    void ToggleWidescreen(RPCParameters* pParams);
    // PlayCrimeReport - ID: 112
    void PlayCrimeReport(RPCParameters* pParams);
    // SetSpawnInfo - ID: 68
    void SetSpawnInfo(RPCParameters* pParams);
    // SetPlayerTeam - ID: 69
    void SetPlayerTeam(RPCParameters* pParams);
    // SetPlayerSkin - ID: 153
    void SetPlayerSkin(RPCParameters* pParams);
    // SetPlayerName - ID: 11
    void SetPlayerName(RPCParameters* pParams);
    // SetPlayerPos - ID: 12
    void SetPlayerPos(RPCParameters* pParams);
    // SetPlayerPosFindZ - ID: 13
    void SetPlayerPosFindZ(RPCParameters* pParams);
    // SetPlayerHealth - ID: 14
    void SetPlayerHealth(RPCParameters* pParams);
    // PutPlayerInVehicle - ID: 70
    void PutPlayerInVehicle(RPCParameters* pParams);
    // RemovePlayerFromVehicle - ID: 71
    void RemovePlayerFromVehicle(RPCParameters* pParams);
    // SetPlayerColor - ID: 72
    void SetPlayerColor(RPCParameters* pParams);
    // ShowGameText - ID: 73
    void ShowGameText(RPCParameters* pParams);
    // SetInterior - ID: 156
    void SetInterior(RPCParameters* pParams);
    // SetCameraPos - ID: 157
    void SetCameraPos(RPCParameters* pParams);
    // SetCameraLookAt - ID: 158
    void SetCameraLookAt(RPCParameters* pParams);
    // SetVehiclePos - ID: 159
    void SetVehiclePos(RPCParameters* pParams);
    // SetVehicleZAngle - ID: 160
    void SetVehicleZAngle(RPCParameters* pParams);
    // SetVehicleParams - ID: 161
    void SetVehicleParams(RPCParameters* pParams);
    // SetCameraBehind - ID: 162
    void SetCameraBehind(RPCParameters* pParams);
    // TogglePlayerControllable - ID: 15
    void TogglePlayerControllable(RPCParameters* pParams);
    // PlaySound - ID: 16
    void PlaySound(RPCParameters* pParams);
    // SetWorldBounds - ID: 17
    void SetWorldBounds(RPCParameters* pParams);
    // GivePlayerMoney - ID: 18
    void GivePlayerMoney(RPCParameters* pParams);
    // SetPlayerFacingAngle - ID: 19
    void SetPlayerFacingAngle(RPCParameters* pParams);
    // ResetPlayerMoney - ID: 20
    void ResetPlayerMoney(RPCParameters* pParams);
    // ResetPlayerWeapons - ID: 21
    void ResetPlayerWeapons(RPCParameters* pParams);
    // GivePlayerWeapon - ID: 22
    void GivePlayerWeapon(RPCParameters* pParams);
    // LinkVehicleToInterior - ID: 65
    void LinkVehicleToInterior(RPCParameters* pParams);
    // SetPlayerArmour - ID: 66
    void SetPlayerArmour(RPCParameters* pParams);
    // SendDeathMessage - ID: 55
    void SendDeathMessage(RPCParameters* pParams);
    // SetShopName - ID: 33
    void SetShopName(RPCParameters* pParams);
    // SetPlayerDrunkLevel - ID: 35
    void SetPlayerDrunkLevel(RPCParameters* pParams);
    // SetArmedWeapon - ID: 67
    void SetArmedWeapon(RPCParameters* pParams);
    // SetPlayerAttachedObject - ID: 113
    void SetPlayerAttachedObject(RPCParameters* pParams);
    // PlayAudioStream - ID: 41
    void PlayAudioStream(RPCParameters* pParams);
    // StopAudioStream - ID: 42
    void StopAudioStream(RPCParameters* pParams);
    // RemoveBuilding - ID: 43
    void RemoveBuilding(RPCParameters* pParams);
    // AttachCameraToObject - ID: 81
    void AttachCameraToObject(RPCParameters* pParams);
    // InterpolateCamera - ID: 82
    void InterpolateCamera(RPCParameters* pParams);
    // ToggleSelectTextDraw - ID: 83
    void ToggleSelectTextDraw(RPCParameters* pParams);
    // SetPlayerObjectMaterial(Text) - ID: 84
    void SetPlayerObjectMaterial(RPCParameters* pParams);
    // Not listed in samp-packet-list wiki.
    // void SetPlayerObjectNoCameraCol(RPCParameters* pParams);
    // ApplyActorAnimation - ID: 173
    void ApplyActorAnimation(RPCParameters* pParams);
    // ClearActorAnimation - ID: 174
    void ClearActorAnimation(RPCParameters* pParams);
    // SetActorFacingAngle - ID: 175
    void SetActorFacingAngle(RPCParameters* pParams);
    // SetActorPos - ID: 176
    void SetActorPos(RPCParameters* pParams);
    // SetActorHealth - ID: 178
    void SetActorHealth(RPCParameters* pParams);
    // InitMenu - ID: 76
    void InitMenu(RPCParameters* pParams);
    // ShowMenu - ID: 77
    void ShowMenu(RPCParameters* pParams);
    // HideMenu - ID: 78
    void HideMenu(RPCParameters* pParams);
    // 0.3.DL only: download and virtual world notifications
    void SetPlayerVirtualWorld(RPCParameters* pParams);  // 48
    void ModelInfo(RPCParameters* pParams);              // 179
    void ModelFile(RPCParameters* pParams);              // 183
    void SrcFinishedDownloading(RPCParameters* pParams); // 185

}
