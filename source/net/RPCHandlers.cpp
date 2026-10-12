#include <stdafx.h>

#include "main.h"

#include "RPCHandlers.h"
#include "CNetGame.h"

#include "CSrvNetStats.h"

#include "game/entities/CPed.h"
#include "game/entities/CObject.h"
#include "game/CAudio.h"
#include "game/BuildingRemoval.h"
#include "gui/CChat.h"
#include "gui/CDialog.h"
#include "gui/CChatBubble.h"
#include "gui/CMenu.h"
#include "gui/CTextDrawSelection.h"
#include "gui/CObjectEdit.h"
#include "gui/CObjectSelection.h"
#include "gui/CDeathWindow.h"
#include "pools/CActorPool.h"
#include "pools/CObjectPool.h"
#include "pools/CVehiclePool.h"
#include "players/CRemotePlayer.h"
#include "sync/AimStuff.h"
#include "pools/CMenuPool.h"
#include "audio/CAudioStream.h"
#include "game/CGame.h"
#include "game/CCamera.h"
#include "game/ModelInfo.h"
#include "pools/CPlayerPool.h"

#include <BitStream.h>
#include <RakClientInterface.h>
#include <StringCompressor.h>

#include <cmath>

// RakNet namespace
using namespace RakNet;

// SA-MP 0.3.DL-1 RPC handler scaffolding.
// The callback ABI intentionally stays RPCParameters*. The declarations inside each
// handler mirror the decoded payload fields and wire order, but no BitStream decoding
// or gameplay behavior is implemented for the remaining scaffolding yet.

namespace
{
    CVehiclePool* NetworkVehicles()
    {
        return g_netGame ? g_netGame->GetVehiclePool() : nullptr;
    }

    CVehicle* NetworkVehicle(std::uint16_t id)
    {
        auto* pool = NetworkVehicles();
        auto* vehicle = pool ? pool->Get(id) : nullptr;
        return vehicle && vehicle->DoesExist() ? vehicle : nullptr;
    }

    CObject* NetworkObject(std::uint16_t id)
    {
        auto* pool = g_netGame ? g_netGame->GetObjectPool() : nullptr;
        return pool ? pool->Get(id) : nullptr;
    }

    struct ObjectMaterialData
    {
        std::uint8_t type{};
        std::uint8_t slot{};
        int model{};
        std::uint32_t color{};
        std::string txd;
        std::string texture;
        std::uint8_t size{};
        std::string font;
        std::uint8_t fontSize{};
        std::uint8_t bold{};
        std::uint8_t alignment{};
        std::uint32_t fontColor{};
        std::uint32_t backgroundColor{};
        std::string text;
    };

    bool ReadObjectMaterial(BitStream& bs, ObjectMaterialData& material)
    {
        if (!bs.Read(material.type) || !bs.Read(material.slot) || material.slot >= CObject::MAX_MATERIALS)
            return false;

        if (material.type == 1)
        {
            std::uint16_t sourceModel{};
            std::uint8_t txdLength{}, textureLength{};
            if (!bs.Read(sourceModel) || !bs.Read(txdLength) || bs.GetNumberOfUnreadBits() < static_cast<unsigned>(txdLength) * 8)
                return false;
            material.txd.resize(txdLength);
            if ((txdLength && !bs.Read(material.txd.data(), txdLength)) || !bs.Read(textureLength) || bs.GetNumberOfUnreadBits() < static_cast<unsigned>(textureLength) * 8)
                return false;
            material.texture.resize(textureLength);
            if ((textureLength && !bs.Read(material.texture.data(), textureLength)) || !bs.Read(material.color))
                return false;

            // R5 RPC material parser maps 0xffff and ids above 20000 to -1.
            material.model = sourceModel == 0xFFFF || sourceModel > 20000 ? -1 : static_cast<int>(sourceModel);
            return true;
        }

        if (material.type == 2)
        {
            std::uint8_t fontLength{};
            if (!bs.Read(material.size) || !bs.Read(fontLength) || bs.GetNumberOfUnreadBits() < static_cast<unsigned>(fontLength) * 8)
                return false;
            material.font.resize(fontLength);
            if ((fontLength && !bs.Read(material.font.data(), fontLength)) || !bs.Read(material.fontSize) || !bs.Read(material.bold) || !bs.Read(material.fontColor) ||
                !bs.Read(material.backgroundColor) || !bs.Read(material.alignment))
                return false;
            char text[2048]{};
            auto* compressor = StringCompressor::Instance();
            if (!compressor || !compressor->DecodeString(text, static_cast<int>(sizeof(text)), &bs))
                return false;
            material.text = text;
            return true;
        }
        return false;
    }

    void ApplyObjectMaterial(CObject* object, const ObjectMaterialData& material)
    {
        if (!object)
            return;
        if (material.type == 1)
            object->SetMaterial(material.model, material.slot, material.txd.c_str(), material.texture.c_str(), material.color);
        else if (material.type == 2)
            object->SetMaterialText(material.slot, material.text.c_str(), static_cast<char>(material.size), material.font.c_str(), static_cast<char>(material.fontSize),
                                    material.bold != 0, material.fontColor, material.backgroundColor, static_cast<char>(material.alignment));
    }

}

// -----------------------------------------------------------------------------
// RPC 34: SetPlayerSkillLevel
// Payload (wire order):
//   uint16 playerId - Player whose skill value is changed.
//   uint32 skillId - GTA weapon-skill index.
//   uint16 level - New skill level.
void RPCHandlers::SetPlayerSkillLevel(RPCParameters* pParams)
{
    if (!pParams || !pParams->input)
        return;

    BitStream bs(pParams->input, BITS_TO_BYTES(pParams->numberOfBitsOfData), false);

    std::uint16_t playerId{};
    std::uint32_t skillId{};
    std::uint16_t level{};

    if (!bs.Read(playerId))
        return;
    if (!bs.Read(skillId))
        return;
    if (!bs.Read(level))
        return;

    auto* pool = g_netGame ? g_netGame->GetPlayerPool() : nullptr;
    if (!pool || playerId >= CPlayerPool::MAX_PLAYERS || skillId >= AimStuff::PLAYER_STAT_COUNT)
        return;
    if (playerId == pool->m_localInfo.m_nId)
    {
        AimStuff::SetLocalPlayerStat(static_cast<int>(skillId), level);
        Log("[rpc:skill] local player=%u, skill=%u, level=%u", playerId, skillId, level);
    }
    else if (auto* remote = pool->GetPlayer(playerId); remote && remote->m_pPed)
        remote->m_pPed->SetPlayerStat(static_cast<int>(skillId), level);
}

// -----------------------------------------------------------------------------
// RPC 36: Create3DTextLabel
// Payload (wire order):
//   uint16 labelId - 3D text label pool ID.
//   uint32 color - ARGB label color.
//   float x - World X coordinate.
//   float y - World Y coordinate.
//   float z - World Z coordinate.
//   float drawDistance - Maximum render distance.
//   uint8 testLos - Whether line-of-sight testing is enabled.
//   uint16 attachedPlayerId - Player attachment ID, or invalid ID.
//   uint16 attachedVehicleId - Vehicle attachment ID, or invalid ID.
//   CSTRING[4096] text - Compressed label text.
void RPCHandlers::Create3DTextLabel(RPCParameters* pParams)
{
    if (!pParams || !pParams->input || !g_netGame || !g_netGame->m_pPools || !g_netGame->m_pPools->m_pLabel)
        return;

    BitStream bs(pParams->input, (pParams->numberOfBitsOfData + 7u) / 8u, false);

    std::uint16_t labelId{};
    std::uint32_t color{};
    CVector position{};
    float drawDistance{};
    std::uint8_t testLos{};
    std::uint16_t attachedPlayerId{};
    std::uint16_t attachedVehicleId{};

    if (!bs.Read(labelId) || !bs.Read(color) || !bs.Read(position.x) || !bs.Read(position.y) || !bs.Read(position.z) || !bs.Read(drawDistance) || !bs.Read(testLos) ||
        !bs.Read(attachedPlayerId) || !bs.Read(attachedVehicleId))
    {
        return;
    }

    char text[4096]{};
    auto* compressor = StringCompressor::Instance();
    if (!compressor || !compressor->DecodeString(text, static_cast<int>(sizeof(text)), &bs))
        return;

    g_netGame->m_pPools->m_pLabel->Create(labelId, text, color, position, drawDistance, testLos != 0, attachedPlayerId, attachedVehicleId);
}

// -----------------------------------------------------------------------------
// RPC 58: Delete3DTextLabel
// Payload (wire order):
//   uint16 labelId - 3D text label pool ID.
void RPCHandlers::Delete3DTextLabel(RPCParameters* pParams)
{
    if (!pParams || !pParams->input || !g_netGame || !g_netGame->m_pPools || !g_netGame->m_pPools->m_pLabel)
        return;

    BitStream bs(pParams->input, (pParams->numberOfBitsOfData + 7u) / 8u, false);
    std::uint16_t labelId{};
    if (bs.Read(labelId))
        g_netGame->m_pPools->m_pLabel->Delete(labelId);
}

// -----------------------------------------------------------------------------
// RPC 59: ChatBubble
// Payload (wire order):
//   uint16 playerId - Player above whom the bubble is shown.
//   uint32 color - ARGB text color.
//   float drawDistance - Maximum bubble render distance.
//   uint32 expireTime - Bubble lifetime in milliseconds.
//   uint8 textLength - Number of text bytes that follow.
//   char[textLength] text - Bubble text.
void RPCHandlers::ChatBubble(RPCParameters* pParams)
{
    if (!pParams || !pParams->input || !g_chatBubble)
        return;

    BitStream bs(pParams->input, (pParams->numberOfBitsOfData + 7u) / 8u, false);

    std::uint16_t playerId{};
    std::uint32_t color{};
    float drawDistance{};
    std::uint32_t expireTime{};
    std::uint8_t textLength{};

    if (!bs.Read(playerId) || !bs.Read(color) || !bs.Read(drawDistance) || !bs.Read(expireTime) || !bs.Read(textLength))
        return;

    std::string text(textLength, '\0');
    if (textLength != 0 && !bs.Read(text.data(), textLength))
        return;

    g_chatBubble->Add(playerId, text.c_str(), color, drawDistance,
                      static_cast<int>(std::min<std::uint32_t>(expireTime, static_cast<std::uint32_t>(std::numeric_limits<int>::max()))));
}

// -----------------------------------------------------------------------------
// RPC 61: ShowDialog
// Payload (wire order):
//   int16 dialogId - Dialog ID.
//   uint8 style - Dialog style.
//   uint8 titleLength - Title length.
//   char[titleLength] title - Dialog title.
//   uint8 button1Length - First button text length.
//   char[button1Length] button1 - First button text.
//   uint8 button2Length - Second button text length.
//   char[button2Length] button2 - Second button text.
//   CSTRING[4096] info - Compressed dialog body text.
void RPCHandlers::ShowDialog(RPCParameters* pParams)
{
    if (!pParams || !pParams->input || !g_dialog)
        return;

    BitStream bs(pParams->input, (pParams->numberOfBitsOfData + 7u) / 8u, false);

    std::int16_t dialogId{};
    std::uint8_t style{};
    std::uint8_t titleLength{};
    std::string title{};
    std::uint8_t button1Length{};
    std::string button1{};
    std::uint8_t button2Length{};
    std::string button2{};

    if (!bs.Read(dialogId) || !bs.Read(style) || !bs.Read(titleLength))
        return;

    title.resize(titleLength);
    if (titleLength != 0 && !bs.Read(title.data(), titleLength))
        return;

    if (!bs.Read(button1Length))
        return;
    button1.resize(button1Length);
    if (button1Length != 0 && !bs.Read(button1.data(), button1Length))
        return;

    if (!bs.Read(button2Length))
        return;
    button2.resize(button2Length);
    if (button2Length != 0 && !bs.Read(button2.data(), button2Length))
        return;

    char info[4096]{};
    auto* compressor = StringCompressor::Instance();
    if (!compressor || !compressor->DecodeString(info, static_cast<int>(sizeof(info)), &bs))
        return;

    g_dialog->Show(dialogId, style, title.c_str(), info, button1.c_str(), button2.c_str(), TRUE);
}

// -----------------------------------------------------------------------------
// RPC 107: SetCheckpoint
// Payload (wire order):
//   float x - Checkpoint X coordinate.
//   float y - Checkpoint Y coordinate.
//   float z - Checkpoint Z coordinate.
//   float radius - Checkpoint radius.
void RPCHandlers::SetCheckpoint(RPCParameters* pParams)
{
    if (!pParams || !pParams->input)
        return;

    BitStream bs(pParams->input, BITS_TO_BYTES(pParams->numberOfBitsOfData), false);

    float x{};
    float y{};
    float z{};
    float radius{};

    if (!bs.Read(x))
        return;
    if (!bs.Read(y))
        return;
    if (!bs.Read(z))
        return;
    if (!bs.Read(radius))
        return;

    if (g_game && radius > 0.0f)
    {
        CVector position{x, y, z}, size{radius, radius, radius};
        g_game->SetCheckpoint(&position, &size);
    }
}

// -----------------------------------------------------------------------------
// RPC 37: DisableCheckpoint
// Payload (wire order):
// Note: No payload fields.
void RPCHandlers::DisableCheckpoint(RPCParameters* pParams)
{
    if (!pParams)
        return;

    BitStream bs;
    (void)bs;

    if (g_game)
        g_game->DisableCheckpoint();
}

// -----------------------------------------------------------------------------
// RPC 38: SetRaceCheckpoint
// Payload (wire order):
//   uint8 type - Race checkpoint type.
//   float x - Current checkpoint X.
//   float y - Current checkpoint Y.
//   float z - Current checkpoint Z.
//   float nextX - Next checkpoint X.
//   float nextY - Next checkpoint Y.
//   float nextZ - Next checkpoint Z.
//   float radius - Checkpoint radius.
void RPCHandlers::SetRaceCheckpoint(RPCParameters* pParams)
{
    if (!pParams || !pParams->input)
        return;

    BitStream bs(pParams->input, BITS_TO_BYTES(pParams->numberOfBitsOfData), false);

    std::uint8_t type{};
    float x{};
    float y{};
    float z{};
    float nextX{};
    float nextY{};
    float nextZ{};
    float radius{};

    if (!bs.Read(type))
        return;
    if (!bs.Read(x))
        return;
    if (!bs.Read(y))
        return;
    if (!bs.Read(z))
        return;
    if (!bs.Read(nextX))
        return;
    if (!bs.Read(nextY))
        return;
    if (!bs.Read(nextZ))
        return;
    if (!bs.Read(radius))
        return;

    if (g_game && radius > 0.0f && type <= 8)
    {
        CVector position{x, y, z}, next{nextX, nextY, nextZ};
        g_game->SetRacingCheckpoint(type, &position, &next, radius);
    }
}

// -----------------------------------------------------------------------------
// RPC 39: DisableRaceCheckpoint
// Payload (wire order):
// Note: No payload fields.
void RPCHandlers::DisableRaceCheckpoint(RPCParameters* pParams)
{
    if (!pParams)
        return;

    BitStream bs;
    (void)bs;

    if (g_game)
        g_game->DeleteRacingCheckpoint();
}

// -----------------------------------------------------------------------------
// RPC 155: UpdateScoresAndPings
// Payload (wire order):
// Note: Payload is a sequence of entries repeated until the BitStream ends.
// Note: Each entry is: uint16 playerId, int32 score, uint32 ping.
void RPCHandlers::UpdateScoresAndPings(RPCParameters* pParams)
{
    if (!pParams || !pParams->input || !g_netGame)
        return;
    auto* playerPool = g_netGame->GetPlayerPool();
    if (!playerPool)
        return;

    BitStream bs(pParams->input, BITS_TO_BYTES(pParams->numberOfBitsOfData), false);

    while (bs.GetNumberOfUnreadBits() >= 80)
    {
        std::uint16_t playerId{};
        std::int32_t score{};
        std::int32_t ping{};

        if (!bs.Read(playerId) || !bs.Read(score) || !bs.Read(ping))
            break;

        if (playerId >= CPlayerPool::MAX_PLAYERS)
            continue;

        playerPool->SetScore(playerId, score);
        playerPool->SetPing(playerId, ping);
    }
}

// -----------------------------------------------------------------------------
// RPC 102: SrvNetStats
// Payload (wire order):
// Note: The DLL reads the server statistics payload as one RakNet statistics block.
// Note: Logical payload: RakNetStatisticsStruct (0x128 bytes in the R5 client).
void RPCHandlers::SrvNetStats(RPCParameters* pParams)
{
    BitStream bs(pParams->input, (pParams->numberOfBitsOfData + 7u) / 8u, false);

    RakNetStatisticsStruct statistics{};
    if (!bs.Read(reinterpret_cast<char*>(&statistics), static_cast<int>(sizeof(statistics))))
        return;

    CSrvNetStats::StoreStatistics(statistics);
}

// -----------------------------------------------------------------------------
// RPC 40: GameModeRestart
// Payload (wire order):
// Note: No payload fields.
void RPCHandlers::GameModeRestart(RPCParameters* pParams)
{
    if (!pParams)
        return;

    if (g_netGame)
        g_netGame->ShutdownForRestart();
}

// -----------------------------------------------------------------------------
// RPC 130: ConnectionRejected
// Payload (wire order):
//   uint8 reason - Connection rejection reason code.
void RPCHandlers::ConnectionRejected(RPCParameters* pParams)
{
    if (!pParams || !pParams->input || pParams->numberOfBitsOfData < 8)
        return;

    BitStream bs(pParams->input, BITS_TO_BYTES(pParams->numberOfBitsOfData), false);

    std::uint8_t reason{};

    if (!bs.Read(reason))
        return;

    if (g_chat)
    {
        switch (reason)
        {
            case 1:
                g_chat->PrintInfo("CONNECTION REJECTED: Incorrect Version.");
                break;
            case 2:
                g_chat->PrintInfo("CONNECTION REJECTED: Unacceptable NickName");
                g_chat->PrintInfo("Please choose another nick between and 3-20 characters");
                g_chat->PrintInfo("Please use only a-z, A-Z, 0-9");
                g_chat->PrintInfo("Use /quit to exit or press ESC and select Quit Game");
                break;
            case 3:
                g_chat->PrintInfo("CONNECTION REJECTED: Bad mod version.");
                break;
            case 4:
                g_chat->PrintInfo("CONNECTION REJECTED: Unable to allocate a player slot.");
                break;
            default:
                break;
        }
    }

    if (auto* client = g_netGame ? g_netGame->GetRakClient() : nullptr)
        client->Disconnect(500);
}

// -----------------------------------------------------------------------------
// RPC 93: SendClientMessage
// Payload (wire order):
//   uint32 color - ARGB message color.
//   uint32 messageLength - Number of message bytes.
//   char[messageLength] message - Message text.
void RPCHandlers::SendClientMessage(RPCParameters* pParams)
{
    const auto dataSize = static_cast<std::uint32_t>((pParams->numberOfBitsOfData + 7u) / 8u);
    if (dataSize < sizeof(std::uint32_t) * 2)
        return;

    BitStream bs(pParams->input, static_cast<int>(dataSize), false);

    std::uint32_t color{};
    std::uint32_t messageLength{};

    if (!bs.Read(color) || !bs.Read(messageLength))
        return;

    if (messageLength > dataSize - sizeof(color) - sizeof(messageLength))
        return;

    std::string message(messageLength, '\0');
    if (messageLength != 0 && !bs.Read(message.data(), static_cast<int>(messageLength)))
        return;

    if (g_chat)
        g_chat->AddMessage(color, message.c_str());
}

// -----------------------------------------------------------------------------
// RPC 94: SetWorldTime
// Payload (wire order):
//   uint8 hour - World hour (0-23).
void RPCHandlers::SetWorldTime(RPCParameters* pParams)
{
    if (!pParams || !pParams->input)
        return;

    BitStream bs(pParams->input, BITS_TO_BYTES(pParams->numberOfBitsOfData), false);

    std::uint8_t hour{};

    if (!bs.Read(hour))
        return;

    if (g_netGame && g_netGame->m_pSettings)
        g_netGame->m_pSettings->m_nWorldTimeHour = hour;
}

// -----------------------------------------------------------------------------
// RPC 95: CreatePickup
// Payload (wire order):
//   int32 pickupId - Pickup pool ID.
//   int32 modelId - GTA model ID.
//   int32 spawnType - Pickup type/spawn behavior.
//   float x - World X.
//   float y - World Y.
//   float z - World Z.
void RPCHandlers::CreatePickup(RPCParameters* pParams)
{
    auto* pool = g_netGame ? g_netGame->GetPickupPool() : nullptr;
    if (!pool || !pParams || !pParams->input || pParams->numberOfBitsOfData < 192)
        return;

    BitStream bs(pParams->input, BITS_TO_BYTES(pParams->numberOfBitsOfData), false);

    std::int32_t pickupId{};
    std::int32_t modelId{};
    std::int32_t spawnType{};
    float x{};
    float y{};
    float z{};

    if (!bs.Read(pickupId))
        return;
    if (!bs.Read(modelId))
        return;
    if (!bs.Read(spawnType))
        return;
    if (!bs.Read(x))
        return;
    if (!bs.Read(y))
        return;
    if (!bs.Read(z))
        return;

    Pickup pickup{modelId, spawnType, {x, y, z}};
    pool->Create(&pickup, pickupId);
}

// -----------------------------------------------------------------------------
// RPC 63: DestroyPickup
// Payload (wire order):
//   int32 pickupId - Pickup pool ID.
void RPCHandlers::DestroyPickup(RPCParameters* pParams)
{
    auto* pool = g_netGame ? g_netGame->GetPickupPool() : nullptr;
    if (!pool || !pParams || !pParams->input || pParams->numberOfBitsOfData < 32)
        return;

    BitStream bs(pParams->input, BITS_TO_BYTES(pParams->numberOfBitsOfData), false);

    std::int32_t pickupId{};

    if (!bs.Read(pickupId))
        return;

    pool->Delete(pickupId);
}

// -----------------------------------------------------------------------------
// RPC 151: DestroyWeaponPickup
// Payload (wire order):
//   uint8 pickupId - Client weapon-pickup slot ID.
// Note: Not documented on samp-packet-list; uint8 payload confirmed from the DL DLL and SAMP.Lua schema.

// legacy
void RPCHandlers::DestroyWeaponPickup(RPCParameters* pParams)
{
    if (!g_netGame->m_pPools || !g_netGame->m_pPools->m_pPickup)
        return;

    BitStream bs(pParams->input, (pParams->numberOfBitsOfData + 7u) / 8u, false);
    std::uint8_t pickupId{};
    if (bs.Read(pickupId))
        g_netGame->m_pPools->m_pPickup->DeleteWeapon(pickupId);
}

// -----------------------------------------------------------------------------
// RPC 96: ScmEvent
// Payload (wire order):
//   uint16 playerId - Player associated with the SCM event.
//   uint32 eventType - SCM event type.
//   uint32 vehicleId - Vehicle ID used by the event.
//   uint32 param1 - Event-specific parameter 1.
//   uint32 param2 - Event-specific parameter 2.
void RPCHandlers::ScmEvent(RPCParameters* pParams)
{
    if (!pParams || !pParams->input)
        return;

    BitStream bs(pParams->input, BITS_TO_BYTES(pParams->numberOfBitsOfData), false);

    std::uint16_t playerId{};
    std::uint32_t eventType{};
    std::uint32_t vehicleId{};
    std::uint32_t param1{};
    std::uint32_t param2{};

    if (!bs.Read(playerId))
        return;
    if (!bs.Read(eventType))
        return;
    if (!bs.Read(vehicleId))
        return;
    if (!bs.Read(param1))
        return;
    if (!bs.Read(param2))
        return;

    if (vehicleId >= CVehiclePool::MAX_VEHICLES)
        return;
    auto* pool = NetworkVehicles();
    if (!pool)
        return;
    auto* vehicle = pool->Get(static_cast<CVehiclePool::ID>(vehicleId));
    switch (eventType)
    {
        case 1: // paintjob
            if (param1 > 3)
                break;
            if (vehicle)
                vehicle->SetPaintjob(static_cast<unsigned char>(param1));
            else
                pool->QueuePaintjob(static_cast<CVehiclePool::ID>(vehicleId), static_cast<unsigned char>(param1));
            break;
        case 2: // component
            if (param1 < 1000 || param1 > 1193)
                break;
            if (vehicle)
                vehicle->AddComponent(static_cast<unsigned short>(param1));
            else
                pool->QueueComponent(static_cast<CVehiclePool::ID>(vehicleId), static_cast<unsigned short>(param1), true);
            break;
        case 3: // palette colors
            if (param1 > 255 || param2 > 255)
                break;
            if (vehicle)
                vehicle->SetColor(static_cast<unsigned char>(param1), static_cast<unsigned char>(param2));
            else
                pool->QueueColors(static_cast<CVehiclePool::ID>(vehicleId), static_cast<unsigned char>(param1), static_cast<unsigned char>(param2));
            break;
        case 4: // modshop entry/exit for the local passenger
        {
            auto* players = g_netGame ? g_netGame->GetPlayerPool() : nullptr;
            auto* remote = players ? players->GetPlayer(playerId) : nullptr;
            auto* local = players ? players->GetLocalPlayer() : nullptr;
            auto* driver = remote ? remote->m_pPed : nullptr;
            auto* passenger = local ? local->m_pPed : nullptr;
            if (!vehicle || !driver || !passenger || !driver->HasNativePed() || !passenger->HasNativePed() || !driver->IsInVehicle() || !passenger->IsInVehicle() ||
                driver->IsPassenger() || !passenger->IsPassenger() || driver->GetVehicle() != passenger->GetVehicle())
                break;

            const int interior = param1 ? static_cast<unsigned char>(param2) : 0;
            if (remote->m_pVehicle)
                remote->m_pVehicle->ChangeInterior(interior);
            passenger->LinkToInterior(static_cast<char>(interior), FALSE);
            passenger->SetControllable(param1 ? FALSE : TRUE);
            break;
        }
        default:
            break;
    }
}

// -----------------------------------------------------------------------------
// RPC 152: SetWeather
// Payload (wire order):
//   uint8 weatherId - GTA weather ID.
void RPCHandlers::SetWeather(RPCParameters* pParams)
{
    if (!pParams || !pParams->input)
        return;

    BitStream bs(pParams->input, BITS_TO_BYTES(pParams->numberOfBitsOfData), false);

    std::uint8_t weatherId{};

    if (!bs.Read(weatherId))
        return;

    if (g_netGame && g_netGame->m_pSettings)
        g_netGame->m_pSettings->m_nWeather = weatherId;
    if (g_game)
        g_game->SetWorldWeather(static_cast<char>(weatherId));
}

// -----------------------------------------------------------------------------
// RPC 29: SetPlayerTime
// Payload (wire order):
//   uint8 hour - Hour.
//   uint8 minute - Minute.
void RPCHandlers::SetPlayerTime(RPCParameters* pParams)
{
    if (!pParams || !pParams->input)
        return;

    BitStream bs(pParams->input, BITS_TO_BYTES(pParams->numberOfBitsOfData), false);

    std::uint8_t hour{};
    std::uint8_t minute{};

    if (!bs.Read(hour))
        return;
    if (!bs.Read(minute))
        return;

    if (g_game)
        g_game->SetWorldTime(static_cast<char>(hour), static_cast<char>(minute));
    if (g_netGame && g_netGame->m_pSettings)
    {
        g_netGame->m_pSettings->m_nWorldTimeHour = hour;
        g_netGame->m_pSettings->m_nWorldTimeMinute = minute;
    }
}

// -----------------------------------------------------------------------------
// RPC 30: ToggleClock
// Payload (wire order):
//   uint8 enabled - 1 enables the HUD clock; 0 disables it.
void RPCHandlers::ToggleClock(RPCParameters* pParams)
{
    if (!pParams || !pParams->input)
        return;

    BitStream bs(pParams->input, BITS_TO_BYTES(pParams->numberOfBitsOfData), false);

    std::uint8_t enabled{};

    if (!bs.Read(enabled))
        return;

    if (!g_game)
        return;

    g_game->EnableClock(enabled != 0);
    if (g_netGame && g_netGame->m_pSettings)
    {
        auto& settings = *g_netGame->m_pSettings;
        settings.m_bHoldTime = enabled == 0;
        if (!enabled)
        {
            char hour{}, minute{};
            g_game->GetWorldTime(&hour, &minute);
            settings.m_nWorldTimeHour = static_cast<unsigned char>(hour);
            settings.m_nWorldTimeMinute = static_cast<unsigned char>(minute);
        }
    }
}

// -----------------------------------------------------------------------------
// RPC 60: SendGameTimeUpdate
// Payload (wire order):
//   int32 time - In-game timer value.
void RPCHandlers::SendGameTimeUpdate(RPCParameters* pParams)
{
    if (!pParams || !pParams->input)
        return;

    BitStream bs(pParams->input, BITS_TO_BYTES(pParams->numberOfBitsOfData), false);

    std::int32_t time{};

    if (!bs.Read(time))
        return;

    if (g_game)
        g_game->SetTimer(time);
}

// -----------------------------------------------------------------------------
// RPC 32: WorldPlayerAdd
// Payload (wire order):
//   uint16 playerId - Remote player ID.
//   uint8 team - Team ID.
//   uint32 baseSkinId - Base GTA skin model.
//   uint32 customSkinId - 0.3.DL custom skin ID; 0 means none.
//   float x - Spawn X.
//   float y - Spawn Y.
//   float z - Spawn Z.
//   float facingAngle - Initial heading.
//   uint32 color - Player color.
//   uint8 fightingStyle - Fighting style ID.
//   uint16[11] skillLevels - Weapon skill levels for the 11 skill slots.
// Note: DL-specific layout: customSkinId is present between baseSkinId and position.
void RPCHandlers::WorldPlayerAdd(RPCParameters* pParams)
{
    Log("WorldPlayerAdd");

    if (!pParams || !pParams->input)
        return;

    BitStream bs(pParams->input, BITS_TO_BYTES(pParams->numberOfBitsOfData), false);

    std::uint16_t playerId{};
    std::uint8_t team{};
    std::uint32_t baseSkinId{};
    std::uint32_t customSkinId{};
    float x{};
    float y{};
    float z{};
    float facingAngle{};
    std::uint32_t color{};
    std::uint8_t fightingStyle{};
    std::array<std::uint16_t, 11> skillLevels{};

    if (!bs.Read(playerId))
        return;
    if (!bs.Read(team))
        return;
    if (!bs.Read(baseSkinId))
        return;
    if (g_DLClient && !bs.Read(customSkinId))
        return;
    if (!bs.Read(x))
        return;
    if (!bs.Read(y))
        return;
    if (!bs.Read(z))
        return;
    if (!bs.Read(facingAngle))
        return;
    if (!bs.Read(color))
        return;
    if (!bs.Read(fightingStyle))
        return;
    for (auto& value : skillLevels)
        if (!bs.Read(value))
            return;

    auto* playerPool = g_netGame ? g_netGame->GetPlayerPool() : nullptr;
    if (!playerPool || playerId >= CPlayerPool::MAX_PLAYERS || playerId == playerPool->m_localInfo.m_nId || baseSkinId > 311)
        return;

    CRemotePlayer* player = playerPool->GetPlayer(playerId);
    if (!player)
        return;

    CVector position{x, y, z};
    if (!player->Spawn(0, static_cast<int>(baseSkinId), 0, &position, facingAngle, color, static_cast<char>(fightingStyle)))
        return;

    playerPool->SetTeam(playerId, team);

    for (std::size_t skill = 0; skill < skillLevels.size(); ++skill)
        player->m_pPed->SetPlayerStat(static_cast<int>(skill), skillLevels[skill]);

    // custom skins need their game-side implementation
    (void)customSkinId;
}

// -----------------------------------------------------------------------------
// RPC 166: DeathBroadcast
// Payload (wire order):
//   uint16 playerId - Remote player ID.
void RPCHandlers::DeathBroadcast(RPCParameters* pParams)
{
    Log("DeathBroadcast");

    if (!pParams || !pParams->input)
        return;

    BitStream bs(pParams->input, BITS_TO_BYTES(pParams->numberOfBitsOfData), false);

    std::uint16_t playerId{};

    if (!bs.Read(playerId))
        return;

    auto* playerPool = g_netGame ? g_netGame->GetPlayerPool() : nullptr;
    if (!playerPool)
        return;

    if (CRemotePlayer* player = playerPool->GetPlayer(playerId))
        player->Kill();
}

// -----------------------------------------------------------------------------
// RPC 163: WorldPlayerRemove
// Payload (wire order):
//   uint16 playerId - Remote player ID.
void RPCHandlers::WorldPlayerRemove(RPCParameters* pParams)
{
    Log("WorldPlayerRemove");

    if (!pParams || !pParams->input)
        return;

    BitStream bs(pParams->input, BITS_TO_BYTES(pParams->numberOfBitsOfData), false);

    std::uint16_t playerId{};

    if (!bs.Read(playerId))
        return;

    auto* playerPool = g_netGame ? g_netGame->GetPlayerPool() : nullptr;
    if (!playerPool)
        return;

    if (CRemotePlayer* player = playerPool->GetPlayer(playerId))
        player->Remove();
}

// -----------------------------------------------------------------------------
// RPC 164: WorldVehicleAdd
// Payload (wire order):
//   uint16 vehicleId - Vehicle pool ID.
//   uint32 modelId - Vehicle model ID.
//   float x - Spawn X.
//   float y - Spawn Y.
//   float z - Spawn Z.
//   float angle - Spawn heading.
//   uint8 color1 - Primary palette color.
//   uint8 color2 - Secondary palette color.
//   float health - Vehicle health.
//   uint8 interior - Interior ID.
//   uint32 doorDamage - Door damage bitfield.
//   uint32 panelDamage - Panel damage bitfield.
//   uint8 lightDamage - Light damage bits.
//   uint8 tireDamage - Tire damage bits.
//   uint8 siren - Whether the vehicle has a siren.
//   uint8[14] mods - Component/mod slots 0..13.
//   uint8 paintJob - Paintjob ID.
//   uint32 bodyColor1 - Extended primary body color.
//   uint32 bodyColor2 - Extended secondary body color.
void RPCHandlers::WorldVehicleAdd(RPCParameters* pParams)
{
    if (!pParams || !pParams->input)
        return;

    BitStream bs(pParams->input, BITS_TO_BYTES(pParams->numberOfBitsOfData), false);

    std::uint16_t vehicleId{};
    std::uint32_t modelId{};
    float x{};
    float y{};
    float z{};
    float angle{};
    std::uint8_t color1{};
    std::uint8_t color2{};
    float health{};
    std::uint8_t interior{};
    std::uint32_t doorDamage{};
    std::uint32_t panelDamage{};
    std::uint8_t lightDamage{};
    std::uint8_t tireDamage{};
    std::uint8_t siren{};
    std::array<std::uint8_t, 14> mods{};
    std::uint8_t paintJob{};
    std::uint32_t bodyColor1{};
    std::uint32_t bodyColor2{};

    if (!bs.Read(vehicleId))
        return;
    if (!bs.Read(modelId))
        return;
    if (!bs.Read(x))
        return;
    if (!bs.Read(y))
        return;
    if (!bs.Read(z))
        return;
    if (!bs.Read(angle))
        return;
    if (!bs.Read(color1))
        return;
    if (!bs.Read(color2))
        return;
    if (!bs.Read(health))
        return;
    if (!bs.Read(interior))
        return;
    if (!bs.Read(doorDamage))
        return;
    if (!bs.Read(panelDamage))
        return;
    if (!bs.Read(lightDamage))
        return;
    if (!bs.Read(tireDamage))
        return;
    if (!bs.Read(siren))
        return;
    for (auto& value : mods)
        if (!bs.Read(value))
            return;
    if (!bs.Read(paintJob))
        return;
    if (!bs.Read(bodyColor1))
        return;
    if (!bs.Read(bodyColor2))
        return;

    auto* pool = NetworkVehicles();
    if (!pool || vehicleId >= CVehiclePool::MAX_VEHICLES || modelId < 400 || modelId > 611 || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) ||
        !std::isfinite(angle) || !std::isfinite(health))
        return;

    CVehiclePool::VehicleInfo info{};
    info.m_nId = vehicleId;
    info.m_nType = static_cast<int>(modelId);
    info.m_position = CVector{x, y, z};
    info.m_fRotation = angle;
    info.m_nPrimaryColor = color1;
    info.m_nSecondaryColor = color2;
    info.m_fHealth = health;
    info.m_nInterior = static_cast<char>(interior);
    info.m_nDoorDamageStatus = static_cast<int>(doorDamage);
    info.m_nPanelDamageStatus = static_cast<int>(panelDamage);
    info.m_nLightDamageStatus = static_cast<char>(lightDamage);
    info.m_nTireDamageStatus = tireDamage;
    info.m_bHasSiren = siren != 0;

    pool->Delete(vehicleId);
    pool->SetSpawnAppearance(vehicleId, mods, paintJob);
    pool->SetSpawnBodyColors(vehicleId, bodyColor1, bodyColor2);
    if (!pool->CreateWithTrainConsist(info))
        pool->AddToWaitingList(&info);
}

// -----------------------------------------------------------------------------
// RPC 165: WorldVehicleRemove
// Payload (wire order):
//   uint16 vehicleId - Vehicle pool ID.
void RPCHandlers::WorldVehicleRemove(RPCParameters* pParams)
{
    if (!pParams || !pParams->input)
        return;

    BitStream bs(pParams->input, BITS_TO_BYTES(pParams->numberOfBitsOfData), false);

    std::uint16_t vehicleId{};

    if (!bs.Read(vehicleId))
        return;

    if (auto* pool = NetworkVehicles())
        pool->Delete(vehicleId);
}

// -----------------------------------------------------------------------------
// RPC 106: UpdateVehicleDamageStatus
// Payload (wire order):
//   uint16 vehicleId - Vehicle pool ID.
//   uint32 panels - Panel damage bitfield.
//   uint32 doors - Door damage bitfield.
//   uint8 lights - Light damage bits.
//   uint8 tires - Tire damage bits.
void RPCHandlers::UpdateVehicleDamageStatus(RPCParameters* pParams)
{
    if (!pParams || !pParams->input)
        return;

    BitStream bs(pParams->input, BITS_TO_BYTES(pParams->numberOfBitsOfData), false);

    std::uint16_t vehicleId{};
    std::uint32_t panels{};
    std::uint32_t doors{};
    std::uint8_t lights{};
    std::uint8_t tires{};

    if (!bs.Read(vehicleId))
        return;
    if (!bs.Read(panels))
        return;
    if (!bs.Read(doors))
        return;
    if (!bs.Read(lights))
        return;
    if (!bs.Read(tires))
        return;

    if (auto* vehicle = NetworkVehicle(vehicleId))
    {
        vehicle->UpdateDamage(static_cast<int>(panels), static_cast<int>(doors), static_cast<char>(lights));
        vehicle->SetTires(static_cast<char>(tires));
    }
    else if (auto* pool = NetworkVehicles())
        pool->QueueDamage(vehicleId, static_cast<int>(panels), static_cast<int>(doors), static_cast<char>(lights), static_cast<char>(tires));
}

// -----------------------------------------------------------------------------
// RPC 24: SetVehicleParamsEx
// Payload (wire order):
//   uint16 vehicleId - Vehicle pool ID.
//   uint8 engine - Engine state.
//   uint8 lights - Lights state.
//   uint8 alarm - Alarm state.
//   uint8 doors - Door-lock state.
//   uint8 bonnet - Bonnet state.
//   uint8 boot - Boot state.
//   uint8 objective - Objective marker state.
//   uint8 siren - Siren/unknown slot in this protocol revision.
//   uint8 doorDriver - Driver door state.
//   uint8 doorPassenger - Front passenger door state.
//   uint8 doorBackLeft - Rear-left door state.
//   uint8 doorBackRight - Rear-right door state.
//   uint8 windowDriver - Driver window state.
//   uint8 windowPassenger - Front passenger window state.
//   uint8 windowBackLeft - Rear-left window state.
//   uint8 windowBackRight - Rear-right window state.
void RPCHandlers::SetVehicleParamsEx(RPCParameters* pParams)
{
    if (!pParams || !pParams->input)
        return;

    BitStream bs(pParams->input, BITS_TO_BYTES(pParams->numberOfBitsOfData), false);

    std::uint16_t vehicleId{};
    std::uint8_t engine{};
    std::uint8_t lights{};
    std::uint8_t alarm{};
    std::uint8_t doors{};
    std::uint8_t bonnet{};
    std::uint8_t boot{};
    std::uint8_t objective{};
    std::uint8_t siren{};
    std::uint8_t doorDriver{};
    std::uint8_t doorPassenger{};
    std::uint8_t doorBackLeft{};
    std::uint8_t doorBackRight{};
    std::uint8_t windowDriver{};
    std::uint8_t windowPassenger{};
    std::uint8_t windowBackLeft{};
    std::uint8_t windowBackRight{};

    if (!bs.Read(vehicleId))
        return;
    if (!bs.Read(engine))
        return;
    if (!bs.Read(lights))
        return;
    if (!bs.Read(alarm))
        return;
    if (!bs.Read(doors))
        return;
    if (!bs.Read(bonnet))
        return;
    if (!bs.Read(boot))
        return;
    if (!bs.Read(objective))
        return;
    if (!bs.Read(siren))
        return;
    if (!bs.Read(doorDriver))
        return;
    if (!bs.Read(doorPassenger))
        return;
    if (!bs.Read(doorBackLeft))
        return;
    if (!bs.Read(doorBackRight))
        return;
    if (!bs.Read(windowDriver))
        return;
    if (!bs.Read(windowPassenger))
        return;
    if (!bs.Read(windowBackLeft))
        return;
    if (!bs.Read(windowBackRight))
        return;

    if (auto* vehicle = NetworkVehicle(vehicleId))
    {
        if (engine <= 1)
            vehicle->SetEngineOn(engine);
        if (lights <= 1)
            vehicle->SetLightsOn(lights);
        if (doors <= 1)
            vehicle->Lock(doors != 0);
        if (alarm <= 1)
            vehicle->SetAlarmState(alarm != 0);
        if (bonnet <= 1)
            vehicle->SetBonnetState(bonnet != 0);
        if (boot <= 1)
            vehicle->SetBootState(boot != 0);
        if (objective <= 1)
            vehicle->SetObjective(objective != 0);
        if (siren <= 1)
            vehicle->EnableSiren(siren != 0);

        const std::array<std::uint8_t, 4> doorStates{doorDriver, doorPassenger, doorBackLeft, doorBackRight};
        for (std::uint8_t index = 0; index < doorStates.size(); ++index)
            if (doorStates[index] <= 1)
                vehicle->SetDoorOpenState(index, doorStates[index] != 0);

        const std::array<std::uint8_t, 4> windows{windowDriver, windowPassenger, windowBackLeft, windowBackRight};
        constexpr std::uint8_t windowComponents[]{10, 8, 11, 9};
        for (std::uint8_t index = 0; index < windows.size(); ++index)
        {
            if (windows[index] == 0)
                vehicle->SetWindowOpenFlag(windowComponents[index]);
            else if (windows[index] == 1)
                vehicle->ClearWindowOpenFlag(windowComponents[index]);
        }
    }
    else if (auto* pool = NetworkVehicles())
        pool->QueueParams(vehicleId, {engine, lights, alarm, doors, bonnet, boot, objective, siren, doorDriver, doorPassenger, doorBackLeft, doorBackRight, windowDriver,
                                      windowPassenger, windowBackLeft, windowBackRight});
}

// -----------------------------------------------------------------------------
// RPC 26: PlayerEnterVehicle
// Payload (wire order):
//   uint16 playerId - Remote player entering the vehicle.
//   uint16 vehicleId - Vehicle ID.
//   uint8 isPassenger - 0 for driver entry, nonzero for passenger entry.
void RPCHandlers::PlayerEnterVehicle(RPCParameters* pParams)
{
    if (!pParams || !pParams->input)
        return;

    BitStream bs(pParams->input, BITS_TO_BYTES(pParams->numberOfBitsOfData), false);

    std::uint16_t playerId{};
    std::uint16_t vehicleId{};
    std::uint8_t isPassenger{};

    if (!bs.Read(playerId))
        return;
    if (!bs.Read(vehicleId))
        return;
    if (!bs.Read(isPassenger))
        return;

    auto* players = g_netGame ? g_netGame->GetPlayerPool() : nullptr;
    auto* vehicles = NetworkVehicles();
    if (!players || !vehicles || vehicleId >= CVehiclePool::MAX_VEHICLES || !vehicles->Get(vehicleId))
        return;
    if (auto* remote = players->GetPlayer(playerId))
        remote->EnterVehicle(vehicleId, isPassenger != 0);
}

// -----------------------------------------------------------------------------
// RPC 154: PlayerExitVehicle
// Payload (wire order):
//   uint16 playerId - Remote player leaving the vehicle.
//   uint16 vehicleId - Vehicle ID.
void RPCHandlers::PlayerExitVehicle(RPCParameters* pParams)
{
    if (!pParams || !pParams->input)
        return;

    BitStream bs(pParams->input, BITS_TO_BYTES(pParams->numberOfBitsOfData), false);

    std::uint16_t playerId{};
    std::uint16_t vehicleId{};

    if (!bs.Read(playerId))
        return;
    if (!bs.Read(vehicleId))
        return;

    auto* players = g_netGame ? g_netGame->GetPlayerPool() : nullptr;
    if (!players || vehicleId >= CVehiclePool::MAX_VEHICLES)
        return;
    if (auto* remote = players->GetPlayer(playerId))
    {
        remote->ExitVehicle(vehicleId);
    }
}

// -----------------------------------------------------------------------------
// RPC 137: ServerJoin
// Payload (wire order):
//   uint16 playerId - Joining player ID.
//   uint32 color - Player color.
//   uint8 isNpc - Nonzero if this slot is an NPC.
//   uint8 nameLength - Name length.
//   char[nameLength] name - Player nickname.
void RPCHandlers::ServerJoin(RPCParameters* pParams)
{
    auto* playerPool = g_netGame->GetPlayerPool();
    if (!playerPool)
        return;

    BitStream bs(pParams->input, BITS_TO_BYTES(pParams->numberOfBitsOfData), false);

    std::uint16_t playerId{};
    std::uint32_t color{};
    std::uint8_t isNpc{};
    std::uint8_t nameLength{};

    if (!bs.Read(playerId) || !bs.Read(color) || !bs.Read(isNpc) || !bs.Read(nameLength))
        return;

    std::string name(nameLength, '\0');

    if (nameLength != 0 && !bs.Read(name.data(), nameLength))
        return;

    if (!playerPool->Create(playerId, name.c_str(), isNpc != 0))
        return;

    playerPool->SetColor(playerId, color);
}

// -----------------------------------------------------------------------------
// RPC 138: ServerQuit
// Payload (wire order):
//   uint16 playerId - Leaving player ID.
//   uint8 reason - 0 timeout, 1 quit, 2 kick/ban.
void RPCHandlers::ServerQuit(RPCParameters* pParams)
{
    auto* playerPool = g_netGame->GetPlayerPool();
    if (!playerPool)
        return;

    BitStream bs(pParams->input, BITS_TO_BYTES(pParams->numberOfBitsOfData), false);

    std::uint16_t playerId{};
    std::uint8_t reason{};

    if (!bs.Read(playerId) || !bs.Read(reason))
        return;

    playerPool->Delete(playerId, reason);
}

// -----------------------------------------------------------------------------
// RPC 139: InitGame
// Payload (wire order):
//   bool zoneNames - Zone-name display flag.
//   bool useCjWalk - CJ walking-style flag.
//   bool allowWeapons - Whether weapons are enabled by the server rules.
//   bool limitGlobalChatRadius - Whether global chat has a distance limit.
//   float globalChatRadius - Global-chat radius.
//   bool stuntBonus - Stunt bonus state.
//   float nameTagDistance - Name-tag draw distance.
//   bool disableEnterExits - Whether enter/exits are disabled.
//   bool nameTagLos - Whether name tags use LOS checks.
//   bool manualVehicleEngineAndLights - Manual engine/lights mode.
//   uint32 spawnsAvailable - Number of class/spawn entries.
//   uint16 playerId - Local player ID assigned by server.
//   bool showNameTags - Global name-tag visibility.
//   uint32 showPlayerMarkers - Player-marker mode.
//   uint8 worldTime - Initial world hour.
//   uint8 weather - Initial weather ID.
//   float gravity - World gravity.
//   bool lanMode - LAN mode flag.
//   uint32 deathDropMoney - Death-drop money setting.
//   bool instagib - Legacy instagib flag.
//   uint32 onFootRate - On-foot sync rate.
//   uint32 inCarRate - In-car sync rate.
//   uint32 weaponRate - Weapon sync rate.
//   uint32 multiplier - Network send multiplier.
//   uint32 lagCompMode - Lag-compensation mode.
//   uint8 hostNameLength - Host-name length.
//   char[hostNameLength] hostName - Server host name.
//   uint8[212] vehicleModels - Used vehicle model flags for model IDs 400..611.
//   uint32 vehicleFriendlyFire - Vehicle-friendly-fire setting.
void RPCHandlers::InitGame(RPCParameters* pParams)
{
    bool zoneNames{};
    bool useCjWalk{};
    bool allowWeapons{};
    bool limitGlobalChatRadius{};
    float globalChatRadius{};
    bool stuntBonus{};
    float nameTagDistance{};
    bool disableEnterExits{};
    bool nameTagLos{};
    bool manualVehicleEngineAndLights{};
    std::uint32_t spawnsAvailable{};
    std::uint16_t playerId{};
    bool showNameTags{};
    std::uint32_t showPlayerMarkers{};
    std::uint8_t worldTime{};
    std::uint8_t weather{};
    float gravity{};
    bool lanMode{};
    std::uint32_t deathDropMoney{};
    bool instagib{};
    std::uint32_t onFootRate{};
    std::uint32_t inCarRate{};
    std::uint32_t weaponRate{};
    std::uint32_t multiplier{};
    std::uint32_t lagCompMode{};
    std::uint8_t hostNameLength{};
    std::string hostName{};
    std::array<std::uint8_t, 212> vehicleModels{};
    std::uint32_t vehicleFriendlyFire{};

    if (!g_netGame->m_pSettings)
        return;

    // new bitstream
    BitStream bs(pParams->input, (pParams->numberOfBitsOfData + 7) / 8, false);

    // check for all info
    if (!bs.Read(zoneNames) || !bs.Read(useCjWalk) || !bs.Read(allowWeapons) || !bs.Read(limitGlobalChatRadius) || !bs.Read(globalChatRadius) || !bs.Read(stuntBonus) ||
        !bs.Read(nameTagDistance) || !bs.Read(disableEnterExits) || !bs.Read(nameTagLos) || !bs.Read(manualVehicleEngineAndLights) || !bs.Read(spawnsAvailable) ||
        !bs.Read(playerId) || !bs.Read(showNameTags) || !bs.Read(showPlayerMarkers) || !bs.Read(worldTime) || !bs.Read(weather) || !bs.Read(gravity) || !bs.Read(lanMode) ||
        !bs.Read(deathDropMoney) || !bs.Read(instagib) || !bs.Read(onFootRate) || !bs.Read(inCarRate) || !bs.Read(weaponRate) || !bs.Read(multiplier) || !bs.Read(lagCompMode) ||
        !bs.Read(hostNameLength))
    {
        return;
    }

    hostName.resize(hostNameLength);
    if (!bs.Read(hostName.data(), hostNameLength) || !bs.Read(reinterpret_cast<char*>(vehicleModels.data()), static_cast<int>(vehicleModels.size())) ||
        !bs.Read(vehicleFriendlyFire) || playerId >= CPlayerPool::MAX_PLAYERS)
    {
        return;
    }

    // set params
    auto& settings = *g_netGame->m_pSettings;
    settings.m_bZoneNames = zoneNames;
    settings.m_bUseCJWalk = useCjWalk;
    settings.m_bAllowWeapons = allowWeapons;
    settings.m_bLtdChatRadius = limitGlobalChatRadius;
    settings.m_fChatRadius = globalChatRadius;
    settings.m_fNameTagsDrawDist = nameTagDistance;
    settings.m_bEnterExit = !disableEnterExits;
    // r5 reads the wire flag, then unconditionally enables nametag LOS at 0x10798
    settings.m_bNoNametagsBehindWalls = true;
    settings.m_bManualVehicleEngineAndLight = manualVehicleEngineAndLights;
    settings.m_bClassesAvailable = spawnsAvailable != 0;
    g_netGame->m_nClassesAvailable = spawnsAvailable;
    settings.m_bNameTags = showNameTags;
    Log("[nametags] enabled=%d, walls=%d, wire walls=%d, distance=%.1f", showNameTags, settings.m_bNoNametagsBehindWalls, nameTagLos, nameTagDistance);
    settings.m_nPlayerMarkersMode = static_cast<int>(showPlayerMarkers);
    settings.m_nWorldTimeHour = worldTime;
    settings.m_nWorldTimeMinute = 0;
    settings.m_nWeather = weather;
    settings.m_fGravity = gravity;
    settings.m_nDeadDropsMoney = deathDropMoney;
    settings.m_bInstagib = instagib;
    settings.m_bVehicleFriendlyFire = vehicleFriendlyFire != 0;
    g_netGame->m_bLanMode = lanMode;

    CLocalPlayer::RefOnfootSendrate() = static_cast<int>(onFootRate);
    CLocalPlayer::RefIncarSendrate() = static_cast<int>(inCarRate);
    CLocalPlayer::RefFiringSendrate() = static_cast<int>(weaponRate);
    CLocalPlayer::RefSendMultiplier() = static_cast<int>(multiplier);
    CNetGame::RefLagCompMode() = lagCompMode;

    strncpy_s(g_netGame->m_szHostname, sizeof(g_netGame->m_szHostname), hostName.c_str(), _TRUNCATE);
    if (auto* players = g_netGame->GetPlayerPool())
        players->m_localInfo.m_nId = playerId;

    g_netGame->SetState(CNetGame::GAME_MODE_CONNECTED);

    g_netGame->ProcessGameStuff();

    Log("[net:init] connected: player=%u, host=%s, classes=%u, rates=%u/%u/%u, multiplier=%u, lagcomp=%u", playerId, hostName.c_str(), spawnsAvailable, onFootRate, inCarRate,
        weaponRate, multiplier, lagCompMode);

    g_chat->PrintDebug("Connected to {B9C9BF}%.64s", hostName.c_str());

    if (auto* players = g_netGame->GetPlayerPool())
    {
        if (auto* local = players->GetLocalPlayer())
        {
            if (!local->m_bDoesSpectating)
                g_netGame->SpawnScreen();
            local->m_classSelection.m_nSelected = 0;
            local->RequestClass(0);
        }
    }
}

// -----------------------------------------------------------------------------
// RPC 101: ChatMessage
// Payload (wire order):
//   uint16 playerId - Player who sent the chat line.
//   uint8 textLength - ChatMessage text length.
//   char[textLength] text - ChatMessage text.
// Note: Server-to-client payload confirmed from the DL DLL; the wiki only lists the opposite-direction ChatMessage payload.
void RPCHandlers::ChatMessage(RPCParameters* pParams)
{
    if (!g_chat)
        return;

    BitStream bs(pParams->input, (pParams->numberOfBitsOfData + 7u) / 8u, false);

    std::uint16_t playerId{};
    std::uint8_t textLength{};
    if (!bs.Read(playerId) || !bs.Read(textLength))
        return;

    std::string text(textLength, '\0');
    if (textLength != 0 && !bs.Read(text.data(), textLength))
        return;

    auto* players = g_netGame->GetPlayerPool();
    if (!players)
        return;

    if (playerId == players->m_localInfo.m_nId)
    {
        auto* local = players->GetLocalPlayer();
        const char* name = players->GetLocalPlayerName();
        if (local && name)
            g_chat->AddChatMessage(name, local->GetColorAsARGB(), text.c_str());
        return;
    }

    if (auto* remote = players->GetPlayer(playerId))
        remote->Chat(text.c_str());
}

// -----------------------------------------------------------------------------
// RPC 128: RequestClass
// Payload (wire order):
//   uint8 response - 0 means the requested class was rejected.
//   uint8 team - Team ID.
//   uint32 baseSkinId - Base GTA skin model.
//   uint32 customSkinId - 0.3.DL custom skin ID; 0 means none.
//   uint8 unused - Unused protocol byte.
//   float x - Spawn X.
//   float y - Spawn Y.
//   float z - Spawn Z.
//   float rotation - Spawn rotation.
//   uint32[3] weapons - Spawn weapon IDs.
//   uint32[3] ammo - Spawn ammunition values.
// Note: If response is 0, the rest of the payload is absent.
// Note: DL-specific layout includes customSkinId.
void RPCHandlers::RequestClass(RPCParameters* pParams)
{
    auto* playerPool = g_netGame->GetPlayerPool();
    auto* localPlayer = playerPool ? playerPool->GetLocalPlayer() : nullptr;
    if (!localPlayer)
        return;

    BitStream bs(pParams->input, (pParams->numberOfBitsOfData + 7) / 8, false);

    std::uint8_t response{};
    if (!bs.Read(response))
        return;

    if (response == 0)
    {
        localPlayer->PrepareForClassSelection_Outcome(FALSE);
        return;
    }

    std::uint8_t team{};
    std::uint32_t baseSkinId{};
    std::uint32_t customSkinId{};
    std::uint8_t unused{};
    CVector position{};
    float rotation{};
    std::array<std::uint32_t, 3> weapons{};
    std::array<std::uint32_t, 3> ammo{};

    if (!bs.Read(team) || !bs.Read(baseSkinId))
        return;

    if (g_DLClient && !bs.Read(customSkinId))
        return;

    if (!bs.Read(unused) || !bs.Read(position.x) || !bs.Read(position.y) || !bs.Read(position.z) || !bs.Read(rotation))
    {
        return;
    }

    for (auto& weapon : weapons)
    {
        if (!bs.Read(weapon))
            return;
    }

    for (auto& value : ammo)
    {
        if (!bs.Read(value))
            return;
    }

    CLocalPlayer::SpawnInfo spawnInfo{};
    spawnInfo.m_nTeam = team;
    spawnInfo.m_nSkin = static_cast<int>(baseSkinId);
    spawnInfo.field_5 = static_cast<char>(unused);
    spawnInfo.m_position = position;
    spawnInfo.m_fRotation = rotation;
    std::memcpy(spawnInfo.m_opaqueLoadout, weapons.data(), sizeof(weapons));
    std::memcpy(spawnInfo.m_opaqueLoadout + sizeof(weapons), ammo.data(), sizeof(ammo));

    localPlayer->m_nTeam = team;
    localPlayer->SetSpawnInfo(&spawnInfo);
    localPlayer->PrepareForClassSelection_Outcome(TRUE);

    Log("[rpc:RequestClass] accepted: class=%d team=%u skin=%u custom=%u", localPlayer->m_classSelection.m_nSelected, team, baseSkinId, customSkinId);
}

// -----------------------------------------------------------------------------
// RPC 129: RequestSpawn
// Payload (wire order):
//   uint8 response - 0 denied, 1 accepted, 2 forced spawn.
void RPCHandlers::RequestSpawn(RPCParameters* pParams)
{
    BitStream bs(pParams->input, BITS_TO_BYTES(pParams->numberOfBitsOfData), false);

    std::uint8_t response{};
    if (!bs.Read(response))
        return;

    if (response > 2)
        return;

    auto* players = g_netGame->GetPlayerPool();
    auto* local = players ? players->GetLocalPlayer() : nullptr;
    if (!local)
        return;

    const bool waiting = local->m_classSelection.m_bWaitingForSpawnRequestReply != FALSE;
    local->m_classSelection.m_bWaitingForSpawnRequestReply = FALSE;

    if (response == 0)
    {
        local->m_bClearedToSpawn = FALSE;
        return;
    }

    if (response == 2 || (response == 1 && waiting))
    {
        local->m_bClearedToSpawn = TRUE;
        local->Spawn();
    }
}

// -----------------------------------------------------------------------------
// RPC 116: EditAttachedObject
// Payload (wire order):
//   uint32 index - Attached-object slot index to edit.
void RPCHandlers::EditAttachedObject(RPCParameters* pParams)
{
    if (!pParams || !pParams->input)
        return;

    BitStream bs(pParams->input, BITS_TO_BYTES(pParams->numberOfBitsOfData), false);

    std::uint32_t index{};

    if (!bs.Read(index))
        return;

    if (g_objectEdit && index < CPed::MAX_ACCESSORIES)
        g_objectEdit->EnterEditAttachedObject(index);
}

// -----------------------------------------------------------------------------
// RPC 117: EditObject
// Payload (wire order):
//   bool playerObject - True for a per-player object; false for a global object.
//   uint16 objectId - Object ID to edit.
void RPCHandlers::EditObject(RPCParameters* pParams)
{
    if (!pParams || !pParams->input)
        return;

    BitStream bs(pParams->input, BITS_TO_BYTES(pParams->numberOfBitsOfData), false);

    bool playerObject{};
    std::uint16_t objectId{};

    if (!bs.Read(playerObject))
        return;
    if (!bs.Read(objectId))
        return;

    if (g_objectEdit)
        g_objectEdit->EnterEditObject(objectId, playerObject);
}

// -----------------------------------------------------------------------------
// RPC 27: EnterEditObject
// Payload (wire order):
// Note: No payload fields; enables object-selection/edit mode.
void RPCHandlers::EnterEditObject(RPCParameters* pParams)
{
    if (!pParams)
        return;

    BitStream bs;
    (void)bs;

    if (g_objectSelection)
        g_objectSelection->Enable(TRUE);
}

// -----------------------------------------------------------------------------
// RPC 28: CancelEdit
// Payload (wire order):
// Note: No payload fields; exits object editing.
void RPCHandlers::CancelEdit(RPCParameters* pParams)
{
    if (!pParams)
        return;

    BitStream bs;
    (void)bs;

    if (g_objectSelection)
        g_objectSelection->Enable(FALSE);
    if (g_objectEdit && g_objectEdit->m_bEnabled)
        g_objectEdit->Disable(FALSE, false);
}

// -----------------------------------------------------------------------------
// RPC 170: ToggleCameraTarget
// Payload (wire order):
//   bool enabled - Whether camera-target update notifications are enabled.
void RPCHandlers::ToggleCameraTarget(RPCParameters* pParams)
{
    if (!pParams || !pParams->input || pParams->numberOfBitsOfData < 1)
        return;

    BitStream bs(pParams->input, BITS_TO_BYTES(pParams->numberOfBitsOfData), false);

    bool enabled{};

    if (!bs.Read(enabled))
        return;

    if (g_netGame)
        g_netGame->m_bUpdateCameraTarget = enabled;
}

// -----------------------------------------------------------------------------
// RPC 103: ClientCheck
// Payload (wire order):
//   uint8 type - Client-check request type.
//   uint32 address - Address/subject value checked by the request.
//   uint16 offset - Offset inside the subject.
//   uint16 count - Number of bytes/elements requested.
void RPCHandlers::ClientCheck(RPCParameters* pParams)
{
    if (!pParams || !pParams->input)
        return;

    BitStream bs(pParams->input, BITS_TO_BYTES(pParams->numberOfBitsOfData), false);

    std::uint8_t type{};
    std::uint32_t address{};
    std::uint16_t offset{};
    std::uint16_t count{};

    if (!bs.Read(type))
        return;
    if (!bs.Read(address))
        return;
    if (!bs.Read(offset))
        return;
    if (!bs.Read(count))
        return;

    // TODO: apply decoded RPC to the game state.
}

// -----------------------------------------------------------------------------
// RPC 171: ShowActor
// Payload (wire order):
//   uint16 actorId - Actor pool ID.
//   uint32 baseSkinId - Base GTA skin model.
//   uint32 customSkinId - 0.3.DL custom skin ID; 0 means none.
//   float x - World X.
//   float y - World Y.
//   float z - World Z.
//   float angle - Actor facing angle.
//   float health - Actor health.
//   uint8 invulnerable - Actor invulnerability state.
// Note: DL-specific layout includes customSkinId.
void RPCHandlers::ShowActor(RPCParameters* pParams)
{
    if (!pParams || !pParams->input)
        return;

    BitStream bs(pParams->input, BITS_TO_BYTES(pParams->numberOfBitsOfData), false);

    std::uint16_t actorId{};
    std::uint32_t baseSkinId{};
    std::uint32_t customSkinId{};
    float x{};
    float y{};
    float z{};
    float angle{};
    float health{};
    std::uint8_t invulnerable{};

    if (!bs.Read(actorId))
        return;
    if (!bs.Read(baseSkinId))
        return;
    if (g_DLClient && !bs.Read(customSkinId))
        return;
    if (!bs.Read(x))
        return;
    if (!bs.Read(y))
        return;
    if (!bs.Read(z))
        return;
    if (!bs.Read(angle))
        return;
    if (!bs.Read(health))
        return;
    if (!bs.Read(invulnerable))
        return;

    auto* pool = g_netGame ? g_netGame->GetActorPool() : nullptr;
    if (!pool || actorId >= CActorPool::MAX_ACTORS || baseSkinId > 30000u || baseSkinId >= static_cast<std::uint32_t>(ModelInfo::MAX_MODELS))
        return;

    const auto* modelInfo = ModelInfo::GetRelocated(static_cast<int>(baseSkinId));
    if (!modelInfo)
        return;

    std::uintptr_t modelType{};
    std::memcpy(&modelType, modelInfo, sizeof(modelType));
    if (modelType != 0x85BDC0u)
        return;

    ActorInfo info{};
    info.m_nId = actorId;
    info.m_nModel = static_cast<int>(baseSkinId);
    info.m_position = CVector{x, y, z};
    info.m_fRotation = angle;
    info.m_fHealth = health;
    info.m_bInvulnerable = invulnerable != 0;
    pool->Create(&info);

    // custom skins need the dl model loader
    (void)customSkinId;
}

// -----------------------------------------------------------------------------
// RPC 172: HideActor
// Payload (wire order):
//   uint16 actorId - Actor pool ID.
void RPCHandlers::HideActor(RPCParameters* pParams)
{
    if (!pParams || !pParams->input)
        return;

    BitStream bs(pParams->input, BITS_TO_BYTES(pParams->numberOfBitsOfData), false);

    std::uint16_t actorId{};

    if (!bs.Read(actorId))
        return;

    if (auto* pool = g_netGame ? g_netGame->GetActorPool() : nullptr)
        pool->Delete(actorId);
}

// -----------------------------------------------------------------------------
// RPC 167: DisableVehicleCollisions
// Payload (wire order):
//   bool disable - True disables collisions between vehicles with drivers.
void RPCHandlers::DisableVehicleCollisions(RPCParameters* pParams)
{
    if (!pParams || !pParams->input || pParams->numberOfBitsOfData < 1)
        return;

    BitStream bs(pParams->input, BITS_TO_BYTES(pParams->numberOfBitsOfData), false);

    bool disable{};

    if (!bs.Read(disable))
        return;

    if (g_netGame)
        g_netGame->m_bDisableCollision = disable;
}

// -----------------------------------------------------------------------------
// RPC 56: SetMapIcon
// Payload (wire order):
//   uint8 iconId - Map-icon slot.
//   float x - World X.
//   float y - World Y.
//   float z - World Z.
//   uint8 type - Radar sprite/icon type.
//   uint32 color - ARGB icon color.
//   uint8 style - Map-icon style.
void RPCHandlers::SetMapIcon(RPCParameters* pParams)
{
    if (!pParams || !pParams->input)
        return;

    BitStream bs(pParams->input, BITS_TO_BYTES(pParams->numberOfBitsOfData), false);

    std::uint8_t iconId{};
    float x{};
    float y{};
    float z{};
    std::uint8_t type{};
    std::uint32_t color{};
    std::uint8_t style{};

    if (!bs.Read(iconId))
        return;
    if (!bs.Read(x))
        return;
    if (!bs.Read(y))
        return;
    if (!bs.Read(z))
        return;
    if (!bs.Read(type))
        return;
    if (!bs.Read(color))
        return;
    if (!bs.Read(style))
        return;

    if (g_netGame && iconId < 100 && type <= 63 && style <= 3)
        g_netGame->CreateMarker(iconId, CVector{x, y, z}, static_cast<char>(type), color, style);
}

// -----------------------------------------------------------------------------
// RPC 144: RemoveMapIcon
// Payload (wire order):
//   uint8 iconId - Map-icon slot to remove.
void RPCHandlers::RemoveMapIcon(RPCParameters* pParams)
{
    if (!pParams || !pParams->input)
        return;

    BitStream bs(pParams->input, BITS_TO_BYTES(pParams->numberOfBitsOfData), false);

    std::uint8_t iconId{};

    if (!bs.Read(iconId))
        return;

    if (g_netGame && iconId < 100)
        g_netGame->DeleteMarker(iconId);
}

// -----------------------------------------------------------------------------
// RPC 145: SetWeaponAmmo
// Payload (wire order):
//   uint8 weaponId - Weapon slot/type ID.
//   uint16 ammo - New ammunition count.
void RPCHandlers::SetWeaponAmmo(RPCParameters* pParams)
{
    if (!pParams || !pParams->input)
        return;

    BitStream bs(pParams->input, BITS_TO_BYTES(pParams->numberOfBitsOfData), false);

    std::uint8_t weaponId{};
    std::uint16_t ammo{};

    if (!bs.Read(weaponId))
        return;
    if (!bs.Read(ammo))
        return;

    if (auto* ped = g_game ? g_game->GetPlayerPed() : nullptr)
        ped->SetWeaponAmmo(weaponId, ammo);
}

// -----------------------------------------------------------------------------
// RPC 146: SetGravity
// Payload (wire order):
//   float gravity - New world gravity.
void RPCHandlers::SetGravity(RPCParameters* pParams)
{
    if (!pParams || !pParams->input)
        return;

    BitStream bs(pParams->input, BITS_TO_BYTES(pParams->numberOfBitsOfData), false);

    float gravity{};

    if (!bs.Read(gravity) || !std::isfinite(gravity))
        return;

    if (g_netGame && g_netGame->m_pSettings)
        g_netGame->m_pSettings->m_fGravity = gravity;
    if (g_game)
        g_game->SetWorldGravity(gravity);
}

// -----------------------------------------------------------------------------
// RPC 147: SetVehicleHealth
// Payload (wire order):
//   uint16 vehicleId - Vehicle pool ID.
//   float health - New vehicle health.
void RPCHandlers::SetVehicleHealth(RPCParameters* pParams)
{
    if (!pParams || !pParams->input)
        return;

    BitStream bs(pParams->input, BITS_TO_BYTES(pParams->numberOfBitsOfData), false);

    std::uint16_t vehicleId{};
    float health{};

    if (!bs.Read(vehicleId))
        return;
    if (!bs.Read(health))
        return;

    if (!std::isfinite(health))
        return;
    if (auto* vehicle = NetworkVehicle(vehicleId))
        vehicle->SetHealth(health);
    else if (auto* pool = NetworkVehicles())
        pool->QueueHealth(vehicleId, health);
}

// -----------------------------------------------------------------------------
// RPC 148: AttachTrailerToVehicle
// Payload (wire order):
//   uint16 trailerId - Trailer vehicle ID.
//   uint16 vehicleId - Towing vehicle ID.
void RPCHandlers::AttachTrailerToVehicle(RPCParameters* pParams)
{
    if (!pParams || !pParams->input)
        return;

    BitStream bs(pParams->input, BITS_TO_BYTES(pParams->numberOfBitsOfData), false);

    std::uint16_t trailerId{};
    std::uint16_t vehicleId{};

    if (!bs.Read(trailerId))
        return;
    if (!bs.Read(vehicleId))
        return;

    auto* towing = NetworkVehicle(vehicleId);
    auto* trailer = NetworkVehicle(trailerId);
    if (towing && trailer && towing != trailer)
    {
        if (auto* attached = towing->GetTrailer(); attached && attached != trailer)
        {
            towing->SetTrailer(attached);
            towing->DetachTrailer();
        }
        towing->SetTrailer(trailer);
        towing->AttachTrailer();
    }
    else if (auto* pool = NetworkVehicles())
        pool->QueueTrailer(vehicleId, trailerId);
}

// -----------------------------------------------------------------------------
// RPC 149: DetachTrailerFromVehicle
// Payload (wire order):
//   uint16 vehicleId - Towing vehicle ID.
void RPCHandlers::DetachTrailerFromVehicle(RPCParameters* pParams)
{
    if (!pParams || !pParams->input)
        return;

    BitStream bs(pParams->input, BITS_TO_BYTES(pParams->numberOfBitsOfData), false);

    std::uint16_t vehicleId{};

    if (!bs.Read(vehicleId))
        return;

    if (auto* pool = NetworkVehicles())
        pool->ClearQueuedTrailer(vehicleId);
    if (auto* towing = NetworkVehicle(vehicleId))
    {
        towing->DetachTrailer();
        towing->SetTrailer(nullptr);
    }
}

// -----------------------------------------------------------------------------
// RPC 44: CreateObject
// Payload (wire order):
//   uint16 objectId - Object pool ID.
//   int32 modelId - Object model ID; DL permits custom model IDs.
//   float x - World X.
//   float y - World Y.
//   float z - World Z.
//   float rotX - Rotation X.
//   float rotY - Rotation Y.
//   float rotZ - Rotation Z.
//   float drawDistance - Object draw distance.
//   uint8 noCameraCollision - Object camera-collision flag.
//   uint16 attachedObjectId - Attached object ID or invalid ID.
//   uint16 attachedVehicleId - Attached vehicle ID or invalid ID.
//   float attachX - Attachment offset X (only if either parent ID is valid).
//   float attachY - Attachment offset Y.
//   float attachZ - Attachment offset Z.
//   float attachRotX - Attachment rotation X.
//   float attachRotY - Attachment rotation Y.
//   float attachRotZ - Attachment rotation Z.
//   uint8 syncRotation - Attachment rotation synchronization flag (only with attachment).
//   uint8 materialCount - Number of material records after the object (maximum 16).
// Note: DL-specific modelId is signed int32.
void RPCHandlers::CreateObject(RPCParameters* pParams)
{
    if (!pParams || !pParams->input)
        return;

    BitStream bs(pParams->input, BITS_TO_BYTES(pParams->numberOfBitsOfData), false);

    std::uint16_t objectId{};
    std::int32_t modelId{};
    float x{};
    float y{};
    float z{};
    float rotX{};
    float rotY{};
    float rotZ{};
    float drawDistance{};
    std::uint8_t noCameraCollision{};
    std::uint16_t attachedObjectId{};
    std::uint16_t attachedVehicleId{};
    float attachX{};
    float attachY{};
    float attachZ{};
    float attachRotX{};
    float attachRotY{};
    float attachRotZ{};
    std::uint8_t syncRotation{};

    if (!bs.Read(objectId))
        return;
    if (!bs.Read(modelId))
        return;
    if (!bs.Read(x))
        return;
    if (!bs.Read(y))
        return;
    if (!bs.Read(z))
        return;
    if (!bs.Read(rotX))
        return;
    if (!bs.Read(rotY))
        return;
    if (!bs.Read(rotZ))
        return;
    if (!bs.Read(drawDistance))
        return;
    if (!bs.Read(noCameraCollision))
        return;
    if (!bs.Read(attachedObjectId))
        return;
    if (!bs.Read(attachedVehicleId))
        return;
    if (attachedObjectId != 0xFFFF || attachedVehicleId != 0xFFFF)
    {
        if (!bs.Read(attachX) || !bs.Read(attachY) || !bs.Read(attachZ) || !bs.Read(attachRotX) || !bs.Read(attachRotY) || !bs.Read(attachRotZ) || !bs.Read(syncRotation))
            return;
    }

    auto* pool = g_netGame ? g_netGame->GetObjectPool() : nullptr;
    if (!pool)
        return;
    std::vector<ObjectMaterialData> materials;
    std::uint8_t materialCount{};
    if (bs.GetNumberOfUnreadBits() >= 8)
    {
        if (!bs.Read(materialCount) || materialCount > CObject::MAX_MATERIALS)
            return;
        materials.reserve(materialCount);
        for (unsigned i = 0; i < materialCount; ++i)
        {
            ObjectMaterialData material{};
            if (!ReadObjectMaterial(bs, material))
                return;
            materials.push_back(std::move(material));
        }
    }

    const CVector position{x, y, z};
    const CVector rotation{rotX, rotY, rotZ};
    if (!pool->Create(objectId, modelId, position, rotation, drawDistance))
        return;
    auto* object = pool->Get(objectId);
    if (!object)
        return;
    object->SetCameraCollision(noCameraCollision == 0);
    const CVector attachOffset{attachX, attachY, attachZ};
    const CVector attachRotation{attachRotX, attachRotY, attachRotZ};
    if (attachedObjectId != CObjectPool::INVALID_ID)
        object->SetAttachedToObject(attachedObjectId, &attachOffset, &attachRotation, static_cast<char>(syncRotation));
    else if (attachedVehicleId != CVehiclePool::INVALID_ID)
        object->SetAttachedToVehicle(attachedVehicleId, &attachOffset, &attachRotation);

    for (const auto& material : materials)
        ApplyObjectMaterial(object, material);
}

// -----------------------------------------------------------------------------
// RPC 45: SetObjectPos
// Payload (wire order):
//   uint16 objectId - Object ID.
//   float x - X.
//   float y - Y.
//   float z - Z.
void RPCHandlers::SetObjectPos(RPCParameters* pParams)
{
    if (!pParams || !pParams->input)
        return;

    BitStream bs(pParams->input, BITS_TO_BYTES(pParams->numberOfBitsOfData), false);

    std::uint16_t objectId{};
    float x{};
    float y{};
    float z{};

    if (!bs.Read(objectId))
        return;
    if (!bs.Read(x))
        return;
    if (!bs.Read(y))
        return;
    if (!bs.Read(z))
        return;

    if (auto* object = NetworkObject(objectId))
    {
        const CVector position{x, y, z};
        object->Teleport(position);
        object->m_position = position;
    }
}

// -----------------------------------------------------------------------------
// RPC 46: SetObjectRotation
// Payload (wire order):
//   uint16 objectId - Object ID.
//   float rotX - Rotation X.
//   float rotY - Rotation Y.
//   float rotZ - Rotation Z.
void RPCHandlers::SetObjectRotation(RPCParameters* pParams)
{
    if (!pParams || !pParams->input)
        return;

    BitStream bs(pParams->input, BITS_TO_BYTES(pParams->numberOfBitsOfData), false);

    std::uint16_t objectId{};
    float rotX{};
    float rotY{};
    float rotZ{};

    if (!bs.Read(objectId))
        return;
    if (!bs.Read(rotX))
        return;
    if (!bs.Read(rotY))
        return;
    if (!bs.Read(rotZ))
        return;

    if (auto* object = NetworkObject(objectId))
    {
        const CVector rotation{rotX, rotY, rotZ};
        object->SetRotation(&rotation);
    }
}

// -----------------------------------------------------------------------------
// RPC 47: DestroyObject
// Payload (wire order):
//   uint16 objectId - Object ID.
void RPCHandlers::DestroyObject(RPCParameters* pParams)
{
    if (!pParams || !pParams->input)
        return;

    BitStream bs(pParams->input, BITS_TO_BYTES(pParams->numberOfBitsOfData), false);

    std::uint16_t objectId{};

    if (!bs.Read(objectId))
        return;

    if (auto* pool = g_netGame ? g_netGame->GetObjectPool() : nullptr)
        pool->Delete(objectId);
}

// -----------------------------------------------------------------------------
// RPC 79: CreateExplosion
// Payload (wire order):
//   float x - Explosion X.
//   float y - Explosion Y.
//   float z - Explosion Z.
//   uint32 type - Explosion type.
//   float radius - Explosion radius.
void RPCHandlers::CreateExplosion(RPCParameters* pParams)
{
    if (!pParams || !pParams->input)
        return;

    BitStream bs(pParams->input, BITS_TO_BYTES(pParams->numberOfBitsOfData), false);

    float x{};
    float y{};
    float z{};
    std::uint32_t type{};
    float radius{};

    if (!bs.Read(x))
        return;
    if (!bs.Read(y))
        return;
    if (!bs.Read(z))
        return;
    if (!bs.Read(type))
        return;
    if (!bs.Read(radius))
        return;

    if (g_game)
        g_game->CreateExplosion(CVector{x, y, z}, static_cast<int>(type), radius);
}

// -----------------------------------------------------------------------------
// RPC 80: ShowPlayerNameTag
// Payload (wire order):
//   uint16 playerId - Target player ID.
//   uint8 show - Nonzero shows the name tag.
void RPCHandlers::ShowPlayerNameTag(RPCParameters* pParams)
{
    if (!pParams || !pParams->input)
        return;

    BitStream bs(pParams->input, BITS_TO_BYTES(pParams->numberOfBitsOfData), false);

    std::uint16_t playerId{};
    std::uint8_t show{};

    if (!bs.Read(playerId))
        return;
    if (!bs.Read(show))
        return;

    if (auto* pool = g_netGame ? g_netGame->GetPlayerPool() : nullptr)
        pool->SetNameTagVisible(playerId, show != 0);
}

// -----------------------------------------------------------------------------
// RPC 99: MoveObject
// Payload (wire order):
//   uint16 objectId - Object ID.
//   float fromX - Current X.
//   float fromY - Current Y.
//   float fromZ - Current Z.
//   float toX - Target X.
//   float toY - Target Y.
//   float toZ - Target Z.
//   float speed - Movement speed.
//   float rotX - Target rotation X.
//   float rotY - Target rotation Y.
//   float rotZ - Target rotation Z.
void RPCHandlers::MoveObject(RPCParameters* pParams)
{
    if (!pParams || !pParams->input)
        return;

    BitStream bs(pParams->input, BITS_TO_BYTES(pParams->numberOfBitsOfData), false);

    std::uint16_t objectId{};
    float fromX{};
    float fromY{};
    float fromZ{};
    float toX{};
    float toY{};
    float toZ{};
    float speed{};
    float rotX{};
    float rotY{};
    float rotZ{};

    if (!bs.Read(objectId))
        return;
    if (!bs.Read(fromX))
        return;
    if (!bs.Read(fromY))
        return;
    if (!bs.Read(fromZ))
        return;
    if (!bs.Read(toX))
        return;
    if (!bs.Read(toY))
        return;
    if (!bs.Read(toZ))
        return;
    if (!bs.Read(speed))
        return;
    if (!bs.Read(rotX))
        return;
    if (!bs.Read(rotY))
        return;
    if (!bs.Read(rotZ))
        return;

    if (auto* object = NetworkObject(objectId))
    {
        const CVector origin{fromX, fromY, fromZ};
        object->Teleport(origin);
        object->m_position = origin;
        object->MoveTo({toX, toY, toZ}, speed, {rotX, rotY, rotZ});
    }
}

// -----------------------------------------------------------------------------
// RPC 122: StopObject
// Payload (wire order):
//   uint16 objectId - Object ID.
void RPCHandlers::StopObject(RPCParameters* pParams)
{
    if (!pParams || !pParams->input)
        return;

    BitStream bs(pParams->input, BITS_TO_BYTES(pParams->numberOfBitsOfData), false);

    std::uint16_t objectId{};

    if (!bs.Read(objectId))
        return;

    if (auto* object = NetworkObject(objectId))
        object->Stop();
}

// -----------------------------------------------------------------------------
// RPC 98: SetVehicleTireStatus
// Payload (wire order):
//   uint16 vehicleId - Vehicle ID.
//   uint8 tires - Tire damage bits.
void RPCHandlers::SetVehicleTireStatus(RPCParameters* pParams)
{
    if (!pParams || !pParams->input)
        return;

    BitStream bs(pParams->input, BITS_TO_BYTES(pParams->numberOfBitsOfData), false);

    std::uint16_t vehicleId{};
    std::uint8_t tires{};

    if (!bs.Read(vehicleId))
        return;
    if (!bs.Read(tires))
        return;

    if (auto* vehicle = NetworkVehicle(vehicleId))
        vehicle->SetTires(static_cast<char>(tires));
    else if (auto* pool = NetworkVehicles())
        pool->QueueTires(vehicleId, static_cast<char>(tires));
}

// -----------------------------------------------------------------------------
// RPC 123: SetVehicleNumberPlate
// Payload (wire order):
//   uint16 vehicleId - Vehicle ID.
//   uint8 plateLength - Plate string length.
//   char[plateLength] plateText - Number-plate text.
void RPCHandlers::SetVehicleNumberPlate(RPCParameters* pParams)
{
    if (!pParams || !pParams->input)
        return;

    BitStream bs(pParams->input, BITS_TO_BYTES(pParams->numberOfBitsOfData), false);

    std::uint16_t vehicleId{};
    std::uint8_t plateLength{};
    std::string plateText{};

    if (!bs.Read(vehicleId) || !bs.Read(plateLength) || bs.GetNumberOfUnreadBits() < plateLength * 8u)
        return;
    plateText.resize(plateLength);
    if (plateLength && !bs.Read(plateText.data(), plateLength))
        return;

    if (auto* vehicle = NetworkVehicle(vehicleId))
        vehicle->SetLicensePlateText(plateText.c_str());
    else if (auto* pool = NetworkVehicles())
        pool->SetSpawnLicensePlate(vehicleId, plateText.c_str());
}

// -----------------------------------------------------------------------------
// RPC 124: TogglePlayerSpectating
// Payload (wire order):
//   uint32 spectating - Nonzero enables spectating.
void RPCHandlers::TogglePlayerSpectating(RPCParameters* pParams)
{
    BitStream bs(pParams->input, BITS_TO_BYTES(pParams->numberOfBitsOfData), false);

    std::uint32_t spectating{};

    if (!bs.Read(spectating))
        return;

    auto* players = g_netGame->GetPlayerPool();
    auto* local = players ? players->GetLocalPlayer() : nullptr;

    if (!local)
        return;

    local->EnableSpectating(spectating != 0);
}

// -----------------------------------------------------------------------------
// RPC 126: SpectatePlayer
// Payload (wire order):
//   uint16 playerId - Player to spectate.
//   uint8 cameraMode - Spectator camera mode.
void RPCHandlers::SpectatePlayer(RPCParameters* pParams)
{
    BitStream bs(pParams->input, BITS_TO_BYTES(pParams->numberOfBitsOfData), false);

    std::uint16_t playerId{};
    std::uint8_t cameraMode{};

    if (!bs.Read(playerId) || !bs.Read(cameraMode))
        return;

    auto* players = g_netGame->GetPlayerPool();
    auto* local = players ? players->GetLocalPlayer() : nullptr;

    if (!local || !local->m_bDoesSpectating)
        return;

    switch (cameraMode)
    {
        case 2:
            local->m_spectating.m_nMode = SPECTATING_MODE_FIXED;
            break;
        case 3:
            local->m_spectating.m_nMode = SPECTATING_MODE_SIDE;
            break;
        default:
            local->m_spectating.m_nMode = SPECTATING_MODE_PLAYER;
            break;
    }

    local->SpectateForPlayer(playerId);
}

// -----------------------------------------------------------------------------
// RPC 127: SpectateVehicle
// Payload (wire order):
//   uint16 vehicleId - Vehicle to spectate.
//   uint8 cameraMode - Spectator camera mode.
void RPCHandlers::SpectateVehicle(RPCParameters* pParams)
{
    BitStream bs(pParams->input, BITS_TO_BYTES(pParams->numberOfBitsOfData), false);

    std::uint16_t vehicleId{};
    std::uint8_t cameraMode{};

    if (!bs.Read(vehicleId) || !bs.Read(cameraMode))
        return;

    auto* players = g_netGame->GetPlayerPool();
    auto* local = players ? players->GetLocalPlayer() : nullptr;

    if (!local || !local->m_bDoesSpectating)
        return;

    switch (cameraMode)
    {
        case 2:
            local->m_spectating.m_nMode = SPECTATING_MODE_FIXED;
            break;
        case 3:
            local->m_spectating.m_nMode = SPECTATING_MODE_SIDE;
            break;
        default:
            local->m_spectating.m_nMode = SPECTATING_MODE_VEHICLE;
            break;
    }

    local->SpectateForVehicle(vehicleId);
}

// -----------------------------------------------------------------------------
// RPC 57: RemoveVehicleComponent
// Payload (wire order):
//   uint16 vehicleId - Vehicle ID.
//   uint16 componentId - Vehicle component to remove.
void RPCHandlers::RemoveVehicleComponent(RPCParameters* pParams)
{
    if (!pParams || !pParams->input)
        return;

    BitStream bs(pParams->input, BITS_TO_BYTES(pParams->numberOfBitsOfData), false);

    std::uint16_t vehicleId{};
    std::uint16_t componentId{};

    if (!bs.Read(vehicleId))
        return;
    if (!bs.Read(componentId))
        return;

    if (auto* vehicle = NetworkVehicle(vehicleId))
        vehicle->RemoveComponent(componentId);
    else if (auto* pool = NetworkVehicles())
        pool->QueueComponent(vehicleId, componentId, false);
}

// -----------------------------------------------------------------------------
// RPC 74: ForceClassSelection
// Payload (wire order):
// Note: No payload fields.
void RPCHandlers::ForceClassSelection(RPCParameters* pParams)
{
    (void)pParams;
    auto* players = g_netGame ? g_netGame->GetPlayerPool() : nullptr;
    auto* local = players ? players->GetLocalPlayer() : nullptr;
    if (local)
        local->m_classSelection.m_bEnableAfterDeath = TRUE;
}

// -----------------------------------------------------------------------------
// RPC 75: AttachObjectToPlayer
// Payload (wire order):
//   uint16 objectId - Object ID.
//   uint16 playerId - Player ID.
//   float x - Attachment offset X.
//   float y - Attachment offset Y.
//   float z - Attachment offset Z.
//   float rotX - Attachment rotation X.
//   float rotY - Attachment rotation Y.
//   float rotZ - Attachment rotation Z.
void RPCHandlers::AttachObjectToPlayer(RPCParameters* pParams)
{
    if (!pParams || !pParams->input)
        return;

    BitStream bs(pParams->input, BITS_TO_BYTES(pParams->numberOfBitsOfData), false);

    std::uint16_t objectId{};
    std::uint16_t playerId{};
    float x{};
    float y{};
    float z{};
    float rotX{};
    float rotY{};
    float rotZ{};

    if (!bs.Read(objectId))
        return;
    if (!bs.Read(playerId))
        return;
    if (!bs.Read(x))
        return;
    if (!bs.Read(y))
        return;
    if (!bs.Read(z))
        return;
    if (!bs.Read(rotX))
        return;
    if (!bs.Read(rotY))
        return;
    if (!bs.Read(rotZ))
        return;

    auto* object = NetworkObject(objectId);
    auto* players = g_netGame ? g_netGame->GetPlayerPool() : nullptr;
    if (!object || !players)
        return;
    CPed* ped = nullptr;
    if (playerId == players->m_localInfo.m_nId)
    {
        if (auto* local = players->GetLocalPlayer())
            ped = local->GetPed();
    }
    else if (auto* remote = players->GetPlayer(playerId))
        ped = remote->m_pPed;
    if (ped)
    {
        const CVector offset{x, y, z};
        const CVector rotation{rotX, rotY, rotZ};
        object->AttachToPlayer(ped, &offset, &rotation);
    }
}

// -----------------------------------------------------------------------------
// RPC 133: SetPlayerWantedLevel
// Payload (wire order):
//   uint8 wantedLevel - New wanted level.
void RPCHandlers::SetPlayerWantedLevel(RPCParameters* pParams)
{
    if (!pParams || !pParams->input)
        return;

    BitStream bs(pParams->input, BITS_TO_BYTES(pParams->numberOfBitsOfData), false);

    std::uint8_t wantedLevel{};

    if (!bs.Read(wantedLevel))
        return;

    if (g_game && wantedLevel <= 6)
        g_game->SetWantedLevel(static_cast<char>(wantedLevel));
}

// -----------------------------------------------------------------------------
// RPC 134: ShowTextDraw
// Payload (wire order):
//   uint16 textDrawId - TextDraw ID.
//   uint8 flags - Box/alignment/proportional flags.
//   float letterWidth - Letter width.
//   float letterHeight - Letter height.
//   uint32 letterColor - Letter ARGB color.
//   float lineWidth - Line/box width.
//   float lineHeight - Line/box height.
//   uint32 boxColor - Box ARGB color.
//   uint8 shadow - Shadow size.
//   uint8 outline - Outline size.
//   uint32 backgroundColor - Background ARGB color.
//   uint8 style - TextDraw font/style.
//   uint8 selectable - Selectable flag.
//   float x - Screen X.
//   float y - Screen Y.
//   uint16 modelId - Preview model ID.
//   float rotX - Preview rotation X.
//   float rotY - Preview rotation Y.
//   float rotZ - Preview rotation Z.
//   float zoom - Preview zoom.
//   int16 color1 - Preview primary color.
//   int16 color2 - Preview secondary color.
//   uint16 textLength - Text length.
//   char[textLength] text - TextDraw text.
void RPCHandlers::ShowTextDraw(RPCParameters* pParams)
{
    if (!pParams || !pParams->input || !g_netGame || !g_netGame->m_pPools || !g_netGame->m_pPools->m_pTextDraw)
        return;

    BitStream bs(pParams->input, (pParams->numberOfBitsOfData + 7u) / 8u, false);

    std::uint16_t textDrawId{};
    CTextDraw::Transmit transmit{};
    std::int16_t color1{};
    std::int16_t color2{};
    std::uint16_t textLength{};

    if (!bs.Read(textDrawId) || !bs.Read(transmit.m_nFlags) || !bs.Read(transmit.m_fLetterWidth) || !bs.Read(transmit.m_fLetterHeight) || !bs.Read(transmit.m_letterColor) ||
        !bs.Read(transmit.m_fBoxWidth) || !bs.Read(transmit.m_fBoxHeight) || !bs.Read(transmit.m_boxColor) || !bs.Read(transmit.m_nShadow) || !bs.Read(transmit.m_bOutline) ||
        !bs.Read(transmit.m_backgroundColor) || !bs.Read(transmit.m_nStyle) || !bs.Read(transmit.m_bSelectable) || !bs.Read(transmit.m_fX) || !bs.Read(transmit.m_fY) ||
        !bs.Read(transmit.m_nModel) || !bs.Read(transmit.m_rotation.x) || !bs.Read(transmit.m_rotation.y) || !bs.Read(transmit.m_rotation.z) || !bs.Read(transmit.m_fZoom) ||
        !bs.Read(color1) || !bs.Read(color2) || !bs.Read(textLength))
    {
        return;
    }

    if (textLength > 800)
        return;

    std::string text(textLength, '\0');
    if (textLength != 0 && !bs.Read(text.data(), textLength))
        return;

    transmit.m_aColor[0] = static_cast<std::uint16_t>(color1);
    transmit.m_aColor[1] = static_cast<std::uint16_t>(color2);
    g_netGame->m_pPools->m_pTextDraw->Create(textDrawId, &transmit, text.c_str());
}

// -----------------------------------------------------------------------------
// RPC 135: HideTextDraw
// Payload (wire order):
//   uint16 textDrawId - TextDraw ID.
void RPCHandlers::HideTextDraw(RPCParameters* pParams)
{
    if (!pParams || !pParams->input || !g_netGame || !g_netGame->m_pPools || !g_netGame->m_pPools->m_pTextDraw)
        return;

    BitStream bs(pParams->input, (pParams->numberOfBitsOfData + 7u) / 8u, false);
    std::uint16_t textDrawId{};
    if (bs.Read(textDrawId))
        g_netGame->m_pPools->m_pTextDraw->Delete(textDrawId);
}

// -----------------------------------------------------------------------------
// RPC 105: TextDrawSetString
// Payload (wire order):
//   uint16 textDrawId - TextDraw ID.
//   uint16 textLength - Text length.
//   char[textLength] text - Replacement text.
void RPCHandlers::TextDrawSetString(RPCParameters* pParams)
{
    if (!pParams || !pParams->input || !g_netGame || !g_netGame->m_pPools || !g_netGame->m_pPools->m_pTextDraw)
        return;

    BitStream bs(pParams->input, (pParams->numberOfBitsOfData + 7u) / 8u, false);

    std::uint16_t textDrawId{};
    std::uint16_t textLength{};
    if (!bs.Read(textDrawId) || !bs.Read(textLength) || textLength > 800)
        return;

    std::string text(textLength, '\0');
    if (textLength != 0 && !bs.Read(text.data(), textLength))
        return;

    if (auto* textDraw = g_netGame->m_pPools->m_pTextDraw->Get(textDrawId))
        textDraw->SetText(text.c_str());
}

// -----------------------------------------------------------------------------
// RPC 108: AddGangZone
// Payload (wire order):
//   int16 gangZoneId - Gang-zone ID.
//   float minX - Minimum X.
//   float minY - Minimum Y.
//   float maxX - Maximum X.
//   float maxY - Maximum Y.
//   uint32 color - Zone ARGB color.
void RPCHandlers::AddGangZone(RPCParameters* pParams)
{
    if (!g_netGame->m_pPools || !g_netGame->m_pPools->m_pGangZone)
        return;

    BitStream bs(pParams->input, (pParams->numberOfBitsOfData + 7u) / 8u, false);

    std::int16_t gangZoneId{};
    float minX{};
    float minY{};
    float maxX{};
    float maxY{};
    std::uint32_t color{};
    if (!bs.Read(gangZoneId) || !bs.Read(minX) || !bs.Read(minY) || !bs.Read(maxX) || !bs.Read(maxY) || !bs.Read(color) || gangZoneId < 0)
        return;

    g_netGame->m_pPools->m_pGangZone->Create(static_cast<std::uint16_t>(gangZoneId), minX, minY, maxX, maxY, color);
}

// -----------------------------------------------------------------------------
// RPC 120: GangZoneDestroy
// Payload (wire order):
//   int16 gangZoneId - Gang-zone ID.
void RPCHandlers::GangZoneDestroy(RPCParameters* pParams)
{
    if (!g_netGame->m_pPools || !g_netGame->m_pPools->m_pGangZone)
        return;

    BitStream bs(pParams->input, (pParams->numberOfBitsOfData + 7u) / 8u, false);
    std::int16_t gangZoneId{};
    if (bs.Read(gangZoneId) && gangZoneId >= 0)
        g_netGame->m_pPools->m_pGangZone->Delete(static_cast<std::uint16_t>(gangZoneId));
}

// -----------------------------------------------------------------------------
// RPC 121: GangZoneFlash
// Payload (wire order):
//   int16 gangZoneId - Gang-zone ID.
//   uint32 color - Flash ARGB color.
void RPCHandlers::GangZoneFlash(RPCParameters* pParams)
{
    if (!g_netGame->m_pPools || !g_netGame->m_pPools->m_pGangZone)
        return;

    BitStream bs(pParams->input, (pParams->numberOfBitsOfData + 7u) / 8u, false);
    std::int16_t gangZoneId{};
    std::uint32_t color{};
    if (bs.Read(gangZoneId) && bs.Read(color) && gangZoneId >= 0)
        g_netGame->m_pPools->m_pGangZone->StartFlashing(static_cast<std::uint16_t>(gangZoneId), color);
}

// -----------------------------------------------------------------------------
// RPC 85: GangZoneStopFlash
// Payload (wire order):
//   int16 gangZoneId - Gang-zone ID.
void RPCHandlers::GangZoneStopFlash(RPCParameters* pParams)
{
    if (!g_netGame->m_pPools || !g_netGame->m_pPools->m_pGangZone)
        return;

    BitStream bs(pParams->input, (pParams->numberOfBitsOfData + 7u) / 8u, false);
    std::int16_t gangZoneId{};
    if (bs.Read(gangZoneId) && gangZoneId >= 0)
        g_netGame->m_pPools->m_pGangZone->StopFlashing(static_cast<std::uint16_t>(gangZoneId));
}

// -----------------------------------------------------------------------------
// RPC 86: ApplyPlayerAnimation
// Payload (wire order):
//   uint16 playerId - Target player ID.
//   uint8 animLibLength - Animation library name length.
//   char[animLibLength] animLib - Animation library name.
//   uint8 animNameLength - Animation name length.
//   char[animNameLength] animName - Animation name.
//   float delta - Animation blend/frame delta.
//   bool loop - Loop flag.
//   bool lockX - Lock X flag.
//   bool lockY - Lock Y flag.
//   bool freeze - Freeze-at-end flag.
//   uint32 time - Animation duration/time.
void RPCHandlers::ApplyPlayerAnimation(RPCParameters* pParams)
{
    if (!g_game)
        return;

    BitStream bs(pParams->input, BITS_TO_BYTES(pParams->numberOfBitsOfData), false);

    std::uint16_t playerId{};
    std::uint8_t animLibLength{};
    std::uint8_t animNameLength{};
    std::array<char, 256> animLib{};
    std::array<char, 256> animName{};

    float delta{};
    bool loop{};
    bool lockX{};
    bool lockY{};
    bool freeze{};
    std::uint32_t time{};

    if (!bs.Read(playerId) || !bs.Read(animLibLength) || !animLibLength)
        return;

    if (!bs.Read(animLib.data(), animLibLength) || !bs.Read(animNameLength) || !animNameLength)
        return;

    if (!bs.Read(animName.data(), animNameLength) || !bs.Read(delta) || !bs.Read(loop) || !bs.Read(lockX) || !bs.Read(lockY) || !bs.Read(freeze) || !bs.Read(time))
        return;

    auto* players = g_netGame->GetPlayerPool();
    if (!players || playerId >= CPlayerPool::MAX_PLAYERS)
        return;

    CPed* ped = nullptr;

    if (playerId == players->m_localInfo.m_nId)
    {
        ped = g_game->GetPlayerPed();
    }
    else if (auto* remote = players->GetPlayer(playerId))
    {
        ped = remote->m_pPed;
    }

    if (!ped || !ped->HasNativePed() || !ped->IsValid())
        return;

    if (!g_game->IsAnimationLoaded(animLib.data()))
    {
        g_game->RequestAnimation(animLib.data());
        g_game->LoadRequestedModels();
    }

    if (!g_game->IsAnimationLoaded(animLib.data()))
        return;

    ped->PerformAnimation(animName.data(), animLib.data(), delta, loop, lockX, lockY, freeze, static_cast<int>(time));
}

// -----------------------------------------------------------------------------
// RPC 87: ClearPlayerAnimation
// Payload (wire order):
//   uint16 playerId - Target player ID.
void RPCHandlers::ClearPlayerAnimation(RPCParameters* pParams)
{
    if (!pParams || !pParams->input)
        return;

    BitStream bs(pParams->input, BITS_TO_BYTES(pParams->numberOfBitsOfData), false);

    std::uint16_t playerId{};

    if (!bs.Read(playerId))
        return;

    auto* pool = g_netGame ? g_netGame->GetPlayerPool() : nullptr;
    if (!pool || playerId >= CPlayerPool::MAX_PLAYERS)
        return;
    CPed* ped = nullptr;
    if (playerId == pool->m_localInfo.m_nId)
        ped = g_game ? g_game->GetPlayerPed() : nullptr;
    else if (auto* remote = pool->GetPlayer(playerId))
        ped = remote->m_pPed;
    if (ped && ped->IsValid())
        ped->ClearTasks();
}

// -----------------------------------------------------------------------------
// RPC 88: SetPlayerSpecialAction
// Payload (wire order):
//   uint8 actionId - Special-action ID.
void RPCHandlers::SetPlayerSpecialAction(RPCParameters* pParams)
{
    if (!pParams || !pParams->input)
        return;

    BitStream bs(pParams->input, BITS_TO_BYTES(pParams->numberOfBitsOfData), false);

    std::uint8_t actionId{};

    if (!bs.Read(actionId))
        return;

    if (auto* pool = g_netGame ? g_netGame->GetPlayerPool() : nullptr)
        if (auto* local = pool->GetLocalPlayer())
            local->SetSpecialAction(static_cast<char>(actionId));
}

// -----------------------------------------------------------------------------
// RPC 104: EnableStuntBonus
// Payload (wire order):
//   uint8 enabled - Nonzero enables stunt bonuses.
void RPCHandlers::EnableStuntBonus(RPCParameters* pParams)
{
    if (!pParams || !pParams->input)
        return;

    BitStream bs(pParams->input, BITS_TO_BYTES(pParams->numberOfBitsOfData), false);

    std::uint8_t enabled{};

    if (!bs.Read(enabled))
        return;

    if (g_game)
        g_game->EnableStuntBonus(enabled != 0);
}

// -----------------------------------------------------------------------------
// RPC 89: SetPlayerFightingStyle
// Payload (wire order):
//   uint16 playerId - Target player ID.
//   uint8 styleId - Fighting-style ID.
void RPCHandlers::SetPlayerFightingStyle(RPCParameters* pParams)
{
    if (!pParams || !pParams->input)
        return;

    BitStream bs(pParams->input, BITS_TO_BYTES(pParams->numberOfBitsOfData), false);

    std::uint16_t playerId{};
    std::uint8_t styleId{};

    if (!bs.Read(playerId))
        return;
    if (!bs.Read(styleId))
        return;

    auto* pool = g_netGame ? g_netGame->GetPlayerPool() : nullptr;
    if (!pool || playerId >= CPlayerPool::MAX_PLAYERS)
        return;
    CPed* ped = nullptr;
    if (playerId == pool->m_localInfo.m_nId)
        ped = g_game ? g_game->GetPlayerPed() : nullptr;
    else if (auto* remote = pool->GetPlayer(playerId))
        ped = remote->m_pPed;
    if (ped && ped->IsValid())
        ped->SetFightingStyle(styleId);
}

// -----------------------------------------------------------------------------
// RPC 90: SetPlayerVelocity
// Payload (wire order):
//   float x - Velocity X.
//   float y - Velocity Y.
//   float z - Velocity Z.
void RPCHandlers::SetPlayerVelocity(RPCParameters* pParams)
{
    if (!pParams || !pParams->input)
        return;

    BitStream bs(pParams->input, BITS_TO_BYTES(pParams->numberOfBitsOfData), false);

    float x{};
    float y{};
    float z{};

    if (!bs.Read(x))
        return;
    if (!bs.Read(y))
        return;
    if (!bs.Read(z))
        return;

    if (std::isfinite(x) && std::isfinite(y) && std::isfinite(z))
        if (auto* ped = g_game ? g_game->GetPlayerPed() : nullptr)
            ped->SetSpeed(CVector{x, y, z});
}

// -----------------------------------------------------------------------------
// RPC 91: SetVehicleVelocity
// Payload (wire order):
//   uint8 turn - 1 applies angular velocity; 0 applies linear velocity.
//   float x - Velocity X.
//   float y - Velocity Y.
//   float z - Velocity Z.
void RPCHandlers::SetVehicleVelocity(RPCParameters* pParams)
{
    if (!pParams || !pParams->input)
        return;

    BitStream bs(pParams->input, BITS_TO_BYTES(pParams->numberOfBitsOfData), false);

    std::uint8_t turn{};
    CVector velocity{};
    if (!bs.Read(turn) || !bs.Read(velocity.x) || !bs.Read(velocity.y) || !bs.Read(velocity.z))
        return;
    if (!std::isfinite(velocity.x) || !std::isfinite(velocity.y) || !std::isfinite(velocity.z))
        return;

    auto* players = g_netGame ? g_netGame->GetPlayerPool() : nullptr;
    auto* local = players ? players->GetLocalPlayer() : nullptr;
    auto* ped = local ? local->GetPed() : nullptr;
    auto* pool = NetworkVehicles();
    if (!ped || !pool || !ped->GetVehicle())
        return;

    const auto vehicleId = pool->Find(ped->GetVehicle());
    auto* vehicle = vehicleId != CVehiclePool::INVALID_ID ? pool->Get(vehicleId) : nullptr;
    if (!vehicle || !vehicle->DoesExist())
        return;

    if (turn)
        vehicle->SetTurnSpeed(velocity);
    else
        vehicle->SetSpeed(velocity);
}

// -----------------------------------------------------------------------------
// RPC 111: ToggleWidescreen
// Payload (wire order):
//   uint8 enabled - Nonzero enables widescreen mode.
void RPCHandlers::ToggleWidescreen(RPCParameters* pParams)
{
    if (!pParams || !pParams->input)
        return;

    BitStream bs(pParams->input, BITS_TO_BYTES(pParams->numberOfBitsOfData), false);

    std::uint8_t enabled{};

    if (!bs.Read(enabled))
        return;

    if (g_game)
        g_game->ToggleWidescreen(enabled != 0);
}

// -----------------------------------------------------------------------------
// RPC 112: PlayCrimeReport
// Payload (wire order):
//   uint16 suspectId - Suspect player ID.
//   uint32 inVehicle - Whether the suspect is in a vehicle.
//   uint32 vehicleModel - Vehicle model.
//   uint32 vehicleColor - Vehicle color.
//   uint32 crime - Crime/report type.
//   float x - World X.
//   float y - World Y.
//   float z - World Z.
void RPCHandlers::PlayCrimeReport(RPCParameters* pParams)
{
    if (!pParams || !pParams->input)
        return;

    BitStream bs(pParams->input, BITS_TO_BYTES(pParams->numberOfBitsOfData), false);

    std::uint16_t suspectId{};
    std::uint32_t inVehicle{};
    std::uint32_t vehicleModel{};
    std::uint32_t vehicleColor{};
    std::uint32_t crime{};
    float x{};
    float y{};
    float z{};

    if (!bs.Read(suspectId))
        return;
    if (!bs.Read(inVehicle))
        return;
    if (!bs.Read(vehicleModel))
        return;
    if (!bs.Read(vehicleColor))
        return;
    if (!bs.Read(crime))
        return;
    if (!bs.Read(x))
        return;
    if (!bs.Read(y))
        return;
    if (!bs.Read(z))
        return;

    if (!g_game || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
        return;

    const CVector position{x, y, z};
    g_game->PlayCrimeReport(static_cast<int>(crime), &position, static_cast<int>(inVehicle), static_cast<int>(vehicleModel), static_cast<int>(vehicleColor));
}

// -----------------------------------------------------------------------------
// RPC 68: SetSpawnInfo
// Payload (wire order):
//   uint8 team - Team ID.
//   uint32 baseSkinId - Base GTA skin model.
//   uint32 customSkinId - 0.3.DL custom skin ID; 0 means none.
//   uint8 unused - Unused protocol byte.
//   float x - Spawn X.
//   float y - Spawn Y.
//   float z - Spawn Z.
//   float rotation - Spawn heading.
//   uint32[3] weapons - Spawn weapon IDs.
//   uint32[3] ammo - Spawn ammunition values.
// Note: DL-specific layout includes customSkinId.
void RPCHandlers::SetSpawnInfo(RPCParameters* pParams)
{
    if (!pParams || !pParams->input)
        return;

    BitStream bs(pParams->input, BITS_TO_BYTES(pParams->numberOfBitsOfData), false);

    std::uint8_t team{};
    std::uint32_t baseSkinId{};
    std::uint32_t customSkinId{};
    std::uint8_t unused{};
    float x{};
    float y{};
    float z{};
    float rotation{};
    std::array<std::uint32_t, 3> weapons{};
    std::array<std::uint32_t, 3> ammo{};

    if (!bs.Read(team))
        return;
    if (!bs.Read(baseSkinId))
        return;
    if (g_DLClient && !bs.Read(customSkinId))
        return;
    if (!bs.Read(unused))
        return;
    if (!bs.Read(x))
        return;
    if (!bs.Read(y))
        return;
    if (!bs.Read(z))
        return;
    if (!bs.Read(rotation))
        return;
    for (auto& value : weapons)
        if (!bs.Read(value))
            return;
    for (auto& value : ammo)
        if (!bs.Read(value))
            return;

    auto* playerPool = g_netGame ? g_netGame->GetPlayerPool() : nullptr;
    auto* local = playerPool ? playerPool->GetLocalPlayer() : nullptr;
    if (!local)
        return;

    CLocalPlayer::SpawnInfo spawnInfo{};
    spawnInfo.m_nTeam = team;
    spawnInfo.m_nSkin = static_cast<int>(baseSkinId);
    spawnInfo.field_5 = static_cast<char>(unused);
    spawnInfo.m_position = CVector{x, y, z};
    spawnInfo.m_fRotation = rotation;
    std::memcpy(spawnInfo.m_opaqueLoadout, weapons.data(), sizeof(weapons));
    std::memcpy(spawnInfo.m_opaqueLoadout + sizeof(weapons), ammo.data(), sizeof(ammo));

    local->SetSpawnInfo(&spawnInfo);
}

// -----------------------------------------------------------------------------
// RPC 69: SetPlayerTeam
// Payload (wire order):
//   uint16 playerId - Target player ID.
//   uint8 teamId - New team ID.
void RPCHandlers::SetPlayerTeam(RPCParameters* pParams)
{
    if (!pParams || !pParams->input)
        return;

    BitStream bs(pParams->input, BITS_TO_BYTES(pParams->numberOfBitsOfData), false);

    std::uint16_t playerId{};
    std::uint8_t teamId{};

    if (!bs.Read(playerId))
        return;
    if (!bs.Read(teamId))
        return;

    if (auto* pool = g_netGame ? g_netGame->GetPlayerPool() : nullptr)
        if (playerId < CPlayerPool::MAX_PLAYERS)
            pool->SetTeam(playerId, teamId);
}

// -----------------------------------------------------------------------------
// RPC 153: SetPlayerSkin
// Payload (wire order):
//   uint16 playerId - Target player ID.
//   uint32 baseSkinId - Base GTA skin model.
//   uint32 customSkinId - 0.3.DL custom skin ID; 0 means none.
// Note: DL-specific layout differs from 0.3.7.
void RPCHandlers::SetPlayerSkin(RPCParameters* pParams)
{
    if (!g_game)
        return;

    BitStream bs(pParams->input, BITS_TO_BYTES(pParams->numberOfBitsOfData), false);

    std::uint16_t playerId{};
    std::uint32_t baseSkinId{};
    std::uint32_t customSkinId{};

    if (g_DLClient)
    {
        if (!bs.Read(playerId) || !bs.Read(baseSkinId) || !bs.Read(customSkinId))
            return;
    }
    else
    {
        std::uint32_t playerId037{};
        if (!bs.Read(playerId037) || !bs.Read(baseSkinId))
            return;
        if (playerId037 > 0xFFFF)
            return;
        playerId = static_cast<std::uint16_t>(playerId037);
    }

    if (playerId >= CPlayerPool::MAX_PLAYERS || baseSkinId > 311)
        return;

    auto* players = g_netGame->GetPlayerPool();
    if (!players)
        return;

    CPed* ped = nullptr;

    if (playerId == players->m_localInfo.m_nId)
    {
        if (auto* local = players->GetLocalPlayer())
            ped = local->GetPed();
    }
    else
    {
        if (auto* remote = players->GetPlayer(playerId))
            ped = remote->m_pPed;
    }

    if (!ped || !ped->IsValid())
        return;

    ped->SetModelIndex(static_cast<int>(baseSkinId));

    // TODO
    (void)customSkinId;
}

// -----------------------------------------------------------------------------
// RPC 11: SetPlayerName
// Payload (wire order):
//   uint16 playerId - Target player ID.
//   uint8 nameLength - Name length.
//   char[nameLength] name - New nickname.
//   uint8 success - Server-side rename result flag.
void RPCHandlers::SetPlayerName(RPCParameters* pParams)
{
    if (!pParams || !pParams->input)
        return;

    BitStream bs(pParams->input, BITS_TO_BYTES(pParams->numberOfBitsOfData), false);

    std::uint16_t playerId{};
    std::uint8_t nameLength{};
    std::string name{};
    std::uint8_t success{};

    if (!bs.Read(playerId) || !bs.Read(nameLength) || bs.GetNumberOfUnreadBits() < nameLength * 8u)
        return;
    name.resize(nameLength);
    if ((nameLength && !bs.Read(name.data(), nameLength)) || !bs.Read(success))
        return;

    if (auto* pool = g_netGame ? g_netGame->GetPlayerPool() : nullptr)
        if (success && playerId < CPlayerPool::MAX_PLAYERS && name.find('\0') == std::string::npos)
            pool->SetName(playerId, name.c_str());
}

// -----------------------------------------------------------------------------
// RPC 12: SetPlayerPos
void RPCHandlers::SetPlayerPos(RPCParameters* pParams)
{
    Log("SetPlayerPos");

    float x{};
    float y{};
    float z{};

    BitStream bs(pParams->input, BITS_TO_BYTES(pParams->numberOfBitsOfData), false);

    if (!bs.Read(x) || !bs.Read(y) || !bs.Read(z))
        return;

    if (!g_netGame)
        return;

    auto* playerPool = g_netGame->GetPlayerPool();
    if (!playerPool)
        return;

    auto* localPlayer = playerPool->GetLocalPlayer();
    if (!localPlayer)
        return;

    auto* ped = localPlayer->GetPed();
    if (!ped)
        return;

    const CVector position{x, y, z};
    ped->Teleport(position);
}

// -----------------------------------------------------------------------------
// RPC 13: SetPlayerPosFindZ
void RPCHandlers::SetPlayerPosFindZ(RPCParameters* pParams)
{
    Log("SetPlayerPosFindZ");

    CVector position{};

    BitStream bs(pParams->input, BITS_TO_BYTES(pParams->numberOfBitsOfData), false);

    if (!bs.Read(position.x) || !bs.Read(position.y) || !bs.Read(position.z))
        return;

    if (!g_game)
        return;

    auto* ped = g_game->GetPlayerPed();
    if (!ped)
        return;

    position.z = g_game->FindGroundZ(position);
    ped->Teleport(position);
}

// -----------------------------------------------------------------------------
// RPC 14: SetPlayerHealth
void RPCHandlers::SetPlayerHealth(RPCParameters* pParams)
{
    Log("SetPlayerHealth");

    float health{};

    BitStream bs(pParams->input, BITS_TO_BYTES(pParams->numberOfBitsOfData), false);

    if (!bs.Read(health))
        return;

    if (!g_netGame)
        return;

    auto* playerPool = g_netGame->GetPlayerPool();
    if (!playerPool)
        return;

    auto* localPlayer = playerPool->GetLocalPlayer();
    if (!localPlayer)
        return;

    auto* ped = localPlayer->GetPed();
    if (!ped)
        return;

    ped->SetHealth(health);
}

// -----------------------------------------------------------------------------
// RPC 70: PutPlayerInVehicle
// Payload (wire order):
//   uint16 vehicleId - Vehicle ID.
//   uint8 seatId - Seat index.
void RPCHandlers::PutPlayerInVehicle(RPCParameters* pParams)
{
    if (!pParams || !pParams->input)
        return;

    BitStream bs(pParams->input, BITS_TO_BYTES(pParams->numberOfBitsOfData), false);

    std::uint16_t vehicleId{};
    std::uint8_t seatId{};
    if (!bs.Read(vehicleId) || !bs.Read(seatId))
        return;

    auto* pool = NetworkVehicles();
    auto* players = g_netGame ? g_netGame->GetPlayerPool() : nullptr;
    auto* local = players ? players->GetLocalPlayer() : nullptr;
    auto* ped = local ? local->GetPed() : nullptr;
    auto* vehicle = pool ? pool->Get(vehicleId) : nullptr;
    if (!ped || !ped->HasNativePed() || !vehicle || !vehicle->DoesExist() || seatId > static_cast<std::uint8_t>(vehicle->GetMaxPassengers()))
        return;

    ped->PutIntoVehicle(vehicle->m_handle, seatId);
    if (ped->GetVehicle() == vehicle->GetNativeVehicleHandle())
        if (auto* players = g_netGame ? g_netGame->GetPlayerPool() : nullptr)
            if (auto* local = players->GetLocalPlayer())
                local->OnServerVehicleWarp(vehicleId);
}

// -----------------------------------------------------------------------------
// RPC 71: RemovePlayerFromVehicle
// Payload (wire order):
// Note: No payload fields.
void RPCHandlers::RemovePlayerFromVehicle(RPCParameters* pParams)
{
    if (!pParams)
        return;

    auto* players = g_netGame ? g_netGame->GetPlayerPool() : nullptr;
    auto* local = players ? players->GetLocalPlayer() : nullptr;
    auto* ped = local ? local->GetPed() : nullptr;
    if (!ped || !ped->HasNativePed() || !ped->GetVehicle())
        return;

    CMatrix matrix = CMatrix::Unity();
    ped->GetMatrix(&matrix);
    ped->WarpFromVehicle(matrix.GetPosition() + matrix.GetRight() * 2.0f);
    if (!ped->GetVehicle())
        if (auto* players = g_netGame ? g_netGame->GetPlayerPool() : nullptr)
            if (auto* local = players->GetLocalPlayer())
                local->OnServerVehicleWarp(CLocalPlayer::INVALID_ID);
}

// -----------------------------------------------------------------------------
// RPC 72: SetPlayerColor
// Payload (wire order):
//   uint16 playerId - Target player ID.
//   uint32 color - New player color.
void RPCHandlers::SetPlayerColor(RPCParameters* pParams)
{
    if (!pParams || !pParams->input)
        return;

    BitStream bs(pParams->input, BITS_TO_BYTES(pParams->numberOfBitsOfData), false);

    std::uint16_t playerId{};
    std::uint32_t color{};

    if (!bs.Read(playerId))
        return;
    if (!bs.Read(color))
        return;

    if (auto* pool = g_netGame ? g_netGame->GetPlayerPool() : nullptr)
        pool->SetColor(playerId, color);
}

// -----------------------------------------------------------------------------
// RPC 73: ShowGameText
// Payload (wire order):
//   uint32 style - Game-text style.
//   uint32 time - Display duration.
//   uint32 messageLength - Text length.
//   char[messageLength] message - Game-text message.
void RPCHandlers::ShowGameText(RPCParameters* pParams)
{
    if (!pParams || !pParams->input)
        return;

    BitStream bs(pParams->input, BITS_TO_BYTES(pParams->numberOfBitsOfData), false);

    std::uint32_t style{};
    std::uint32_t time{};
    std::uint32_t messageLength{};
    std::string message{};

    if (!bs.Read(style) || !bs.Read(time) || !bs.Read(messageLength) || messageLength > 16384 || bs.GetNumberOfUnreadBits() < messageLength * 8u)
        return;
    message.resize(messageLength);
    if (messageLength && !bs.Read(message.data(), static_cast<int>(messageLength)))
        return;

    if (g_game && style <= 6 && time <= INT_MAX)
        g_game->DisplayGameText(message.c_str(), static_cast<int>(time), static_cast<int>(style));
}

// -----------------------------------------------------------------------------
// RPC 156: SetInterior
// Payload (wire order):
//   uint8 interiorId - New local-player interior ID.
void RPCHandlers::SetInterior(RPCParameters* pParams)
{
    if (!g_game)
        return;

    BitStream bs(pParams->input, BITS_TO_BYTES(pParams->numberOfBitsOfData), false);

    std::uint8_t interiorId{};

    if (!bs.Read(interiorId))
        return;

    auto* ped = g_game->GetPlayerPed();
    if (!ped || !ped->HasNativePed() || !ped->IsValid())
        return;

    ped->LinkToInterior(static_cast<char>(interiorId), TRUE);
}

// -----------------------------------------------------------------------------
// RPC 157: SetCameraPos
// Payload (wire order):
//   float x - Camera X.
//   float y - Camera Y.
//   float z - Camera Z.
void RPCHandlers::SetCameraPos(RPCParameters* pParams)
{
    if (!g_game)
        return;

    BitStream bs(pParams->input, BITS_TO_BYTES(pParams->numberOfBitsOfData), false);

    CVector position{};

    if (!bs.Read(position.x) || !bs.Read(position.y) || !bs.Read(position.z))
        return;

    auto* camera = g_game->GetCamera();
    if (!camera)
        return;

    camera->Set(position, CVector{});
}

// -----------------------------------------------------------------------------
// RPC 158: SetCameraLookAt
// Payload (wire order):
//   float x - Look-at X.
//   float y - Look-at Y.
//   float z - Look-at Z.
//   uint8 cutType - Camera transition/cut type.
void RPCHandlers::SetCameraLookAt(RPCParameters* pParams)
{
    if (!g_game)
        return;

    BitStream bs(pParams->input, BITS_TO_BYTES(pParams->numberOfBitsOfData), false);

    CVector position{};
    std::uint8_t cutType{};

    if (!bs.Read(position.x) || !bs.Read(position.y) || !bs.Read(position.z) || !bs.Read(cutType))
        return;

    if (cutType != 1 && cutType != 2)
        cutType = 2;

    auto* camera = g_game->GetCamera();
    if (!camera)
        return;

    camera->PointAt(position, cutType);
}

// -----------------------------------------------------------------------------
// RPC 159: SetVehiclePos
// Payload (wire order):
//   uint16 vehicleId - Vehicle ID.
//   float x - X.
//   float y - Y.
//   float z - Z.
void RPCHandlers::SetVehiclePos(RPCParameters* pParams)
{
    if (!pParams || !pParams->input)
        return;

    BitStream bs(pParams->input, BITS_TO_BYTES(pParams->numberOfBitsOfData), false);

    std::uint16_t vehicleId{};
    float x{};
    float y{};
    float z{};

    if (!bs.Read(vehicleId))
        return;
    if (!bs.Read(x))
        return;
    if (!bs.Read(y))
        return;
    if (!bs.Read(z))
        return;

    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
        return;
    if (auto* vehicle = NetworkVehicle(vehicleId))
        vehicle->Teleport(CVector{x, y, z});
    else if (auto* pool = NetworkVehicles())
        pool->QueuePosition(vehicleId, CVector{x, y, z});
}

// -----------------------------------------------------------------------------
// RPC 160: SetVehicleZAngle
// Payload (wire order):
//   uint16 vehicleId - Vehicle ID.
//   float angle - New Z heading.
void RPCHandlers::SetVehicleZAngle(RPCParameters* pParams)
{
    if (!pParams || !pParams->input)
        return;

    BitStream bs(pParams->input, BITS_TO_BYTES(pParams->numberOfBitsOfData), false);

    std::uint16_t vehicleId{};
    float angle{};

    if (!bs.Read(vehicleId))
        return;
    if (!bs.Read(angle))
        return;

    if (!std::isfinite(angle))
        return;
    if (auto* vehicle = NetworkVehicle(vehicleId))
        vehicle->SetRotation(angle);
    else if (auto* pool = NetworkVehicles())
        pool->QueueRotation(vehicleId, angle);
}

// -----------------------------------------------------------------------------
// RPC 161: SetVehicleParams
// Payload (wire order):
//   uint16 vehicleId - Vehicle ID.
//   uint8 objective - Objective marker state.
//   uint8 doorsLocked - Door lock state.
void RPCHandlers::SetVehicleParams(RPCParameters* pParams)
{
    if (!pParams || !pParams->input)
        return;

    BitStream bs(pParams->input, BITS_TO_BYTES(pParams->numberOfBitsOfData), false);

    std::uint16_t vehicleId{};
    std::uint8_t objective{};
    std::uint8_t doorsLocked{};

    if (!bs.Read(vehicleId))
        return;
    if (!bs.Read(objective))
        return;
    if (!bs.Read(doorsLocked))
        return;

    if (auto* pool = NetworkVehicles())
    {
        if (pool->IsPending(vehicleId))
            pool->QueueSimpleParams(vehicleId, objective != 0, doorsLocked != 0);
        else
            pool->SetParams(vehicleId, objective != 0, doorsLocked != 0);
    }
}

// -----------------------------------------------------------------------------
// RPC 162: SetCameraBehind
// Payload (wire order):
// Note: No payload fields.
// RPC 162: SetCameraBehind
// Payload (wire order):
// Note: No payload fields.
void RPCHandlers::SetCameraBehind(RPCParameters* pParams)
{
    if (!pParams)
        return;

    BitStream bs;
    (void)bs;

    if (!g_game)
        return;

    auto* camera = g_game->GetCamera();
    if (!camera)
        return;

    camera->Restore();
}

// -----------------------------------------------------------------------------
// RPC 15: TogglePlayerControllable
// Payload (wire order):
//   uint8 moveable - 1 enables control; 0 freezes/disables control.
void RPCHandlers::TogglePlayerControllable(RPCParameters* pParams)
{
    if (!pParams || !pParams->input)
        return;

    BitStream bs(pParams->input, BITS_TO_BYTES(pParams->numberOfBitsOfData), false);

    std::uint8_t moveable{};

    if (!bs.Read(moveable))
        return;

    if (auto* ped = g_game ? g_game->GetPlayerPed() : nullptr)
        ped->SetControllable(moveable != 0);
}

// -----------------------------------------------------------------------------
// RPC 16: PlaySound
// Payload (wire order):
//   uint32 soundId - GTA sound ID.
//   float x - Sound X.
//   float y - Sound Y.
//   float z - Sound Z.
void RPCHandlers::PlaySound(RPCParameters* pParams)
{
    if (!pParams || !pParams->input)
        return;

    BitStream bs(pParams->input, BITS_TO_BYTES(pParams->numberOfBitsOfData), false);

    std::uint32_t soundId{};
    float x{};
    float y{};
    float z{};

    if (!bs.Read(soundId))
        return;
    if (!bs.Read(x))
        return;
    if (!bs.Read(y))
        return;
    if (!bs.Read(z))
        return;

    if (auto* audio = g_game ? g_game->GetAudio() : nullptr)
        audio->Play(static_cast<std::int32_t>(soundId), CVector{x, y, z});
}

// -----------------------------------------------------------------------------
// RPC 17: SetWorldBounds
// Payload (wire order):
//   float maxX - Maximum X.
//   float minX - Minimum X.
//   float maxY - Maximum Y.
//   float minY - Minimum Y.
void RPCHandlers::SetWorldBounds(RPCParameters* pParams)
{
    if (!pParams || !pParams->input)
        return;

    BitStream bs(pParams->input, BITS_TO_BYTES(pParams->numberOfBitsOfData), false);

    float maxX{};
    float minX{};
    float maxY{};
    float minY{};

    if (!bs.Read(maxX))
        return;
    if (!bs.Read(minX))
        return;
    if (!bs.Read(maxY))
        return;
    if (!bs.Read(minY))
        return;

    if (!g_netGame || !g_netGame->m_pSettings || !std::isfinite(maxX) || !std::isfinite(minX) || !std::isfinite(maxY) || !std::isfinite(minY))
        return;

    auto& bounds = g_netGame->m_pSettings->m_fWorldBoundaries;
    bounds[0] = maxX;
    bounds[1] = minX;
    bounds[2] = maxY;
    bounds[3] = minY;
}

// -----------------------------------------------------------------------------
// RPC 18: GivePlayerMoney
// Payload (wire order):
//   int32 money - Money delta to add.
void RPCHandlers::GivePlayerMoney(RPCParameters* pParams)
{
    if (!pParams || !pParams->input)
        return;

    BitStream bs(pParams->input, BITS_TO_BYTES(pParams->numberOfBitsOfData), false);

    std::int32_t money{};

    if (!bs.Read(money))
        return;

    if (g_game)
        g_game->IncreasePlayerMoney(money);
}

// -----------------------------------------------------------------------------
// RPC 19: SetPlayerFacingAngle
// Payload (wire order):
//   float angle - New local-player heading.
void RPCHandlers::SetPlayerFacingAngle(RPCParameters* pParams)
{
    if (!g_game)
        return;

    BitStream bs(pParams->input, BITS_TO_BYTES(pParams->numberOfBitsOfData), false);

    float angle{};

    if (!bs.Read(angle))
        return;

    auto* ped = g_game->GetPlayerPed();
    if (!ped || !ped->HasNativePed() || !ped->IsValid())
        return;

    ped->SetRotation(angle);
}

// -----------------------------------------------------------------------------
// RPC 20: ResetPlayerMoney
// Payload (wire order):
// Note: No payload fields.
void RPCHandlers::ResetPlayerMoney(RPCParameters* pParams)
{
    if (!pParams)
        return;

    BitStream bs;
    (void)bs;

    if (g_game)
        g_game->ResetMoney();
}

// -----------------------------------------------------------------------------
// RPC 21: ResetPlayerWeapons
// Payload (wire order):
// Note: No payload fields.
void RPCHandlers::ResetPlayerWeapons(RPCParameters* pParams)
{
    if (!pParams)
        return;

    BitStream bs;
    (void)bs;

    if (auto* ped = g_game ? g_game->GetPlayerPed() : nullptr)
        ped->ClearWeapons();
}

// -----------------------------------------------------------------------------
// RPC 22: GivePlayerWeapon
// Payload (wire order):
//   uint32 weaponId - In-game weapon ID.
//   uint32 ammo - In-game ammunition amount.
void RPCHandlers::GivePlayerWeapon(RPCParameters* pParams)
{
    if (!pParams || !pParams->input)
        return;

    BitStream bs(pParams->input, BITS_TO_BYTES(pParams->numberOfBitsOfData), false);

    std::uint32_t weaponId{};
    std::uint32_t ammo{};

    if (!bs.Read(weaponId))
        return;
    if (!bs.Read(ammo))
        return;

    if (weaponId <= 46 && ammo <= INT_MAX)
        if (auto* ped = g_game ? g_game->GetPlayerPed() : nullptr)
            ped->GiveWeapon(static_cast<int>(weaponId), static_cast<int>(ammo));
}

// -----------------------------------------------------------------------------
// RPC 65: LinkVehicleToInterior
// Payload (wire order):
//   uint16 vehicleId - Vehicle ID.
//   uint8 interiorId - Interior ID.
void RPCHandlers::LinkVehicleToInterior(RPCParameters* pParams)
{
    if (!pParams || !pParams->input)
        return;

    BitStream bs(pParams->input, BITS_TO_BYTES(pParams->numberOfBitsOfData), false);

    std::uint16_t vehicleId{};
    std::uint8_t interiorId{};

    if (!bs.Read(vehicleId))
        return;
    if (!bs.Read(interiorId))
        return;

    if (auto* pool = NetworkVehicles())
    {
        if (pool->IsPending(vehicleId))
            pool->QueueInterior(vehicleId, interiorId);
        else
            pool->ChangeInterior(vehicleId, interiorId);
    }
}

// -----------------------------------------------------------------------------
// RPC 66: SetPlayerArmour
// Payload (wire order):
//   float armor - New local-player armour value.
void RPCHandlers::SetPlayerArmour(RPCParameters* pParams)
{
    if (!pParams || !pParams->input)
        return;

    BitStream bs(pParams->input, BITS_TO_BYTES(pParams->numberOfBitsOfData), false);

    float armor{};

    if (!bs.Read(armor))
        return;

    if (std::isfinite(armor))
        if (auto* ped = g_game ? g_game->GetPlayerPed() : nullptr)
            ped->SetArmour(armor);
}

// -----------------------------------------------------------------------------
// RPC 55: SendDeathMessage
// Payload (wire order):
//   uint16 killerId - Killer player ID.
//   uint16 playerId - Victim player ID.
//   uint8 reason - Death reason / in-game weapon reason code.
void RPCHandlers::SendDeathMessage(RPCParameters* pParams)
{
    if (!pParams || !pParams->input)
        return;

    BitStream bs(pParams->input, BITS_TO_BYTES(pParams->numberOfBitsOfData), false);

    std::uint16_t killerId{};
    std::uint16_t playerId{};
    std::uint8_t reason{};

    if (!bs.Read(killerId))
        return;
    if (!bs.Read(playerId))
        return;
    if (!bs.Read(reason))
        return;

    auto* pool = g_netGame ? g_netGame->GetPlayerPool() : nullptr;
    if (!pool || !g_deathWindow)
        return;
    const auto color = [pool](std::uint16_t id) -> D3DCOLOR {
        if (id == pool->m_localInfo.m_nId && pool->GetLocalPlayer())
            return pool->GetLocalPlayer()->GetColorAsARGB();
        auto* player = pool->GetPlayer(id);
        return player ? player->GetColorAsARGB() : 0xFFFFFFFFu;
    };
    g_deathWindow->AddMessage(pool->GetName(killerId), pool->GetName(playerId), color(killerId), color(playerId), static_cast<char>(reason));
}

// -----------------------------------------------------------------------------
// RPC 33: SetShopName
// Payload: fixed 32-byte shop name buffer, as read by R5 and DL.
void RPCHandlers::SetShopName(RPCParameters* pParams)
{
    if (!pParams || !pParams->input || pParams->numberOfBitsOfData < 32u * 8u)
        return;

    BitStream bs(pParams->input, BITS_TO_BYTES(pParams->numberOfBitsOfData), false);
    std::array<char, 33> name{};
    if (!bs.Read(name.data(), 32))
        return;

    auto* ped = g_game ? g_game->GetPlayerPed() : nullptr;
    if (!ped)
        return;

    ped->SetShopName(name[0] ? name.data() : nullptr);
    ped->LoadShoppingDataSubsection(name.data());
}

// -----------------------------------------------------------------------------
// RPC 35: SetPlayerDrunkLevel
// Payload (wire order):
//   int32 drunkLevel - Drunk-level value.
void RPCHandlers::SetPlayerDrunkLevel(RPCParameters* pParams)
{
    if (!pParams || !pParams->input)
        return;

    BitStream bs(pParams->input, BITS_TO_BYTES(pParams->numberOfBitsOfData), false);

    std::int32_t drunkLevel{};

    if (!bs.Read(drunkLevel))
        return;

    if (auto* ped = g_game ? g_game->GetPlayerPed() : nullptr)
        ped->SetDrunkLevel(drunkLevel);
}

// -----------------------------------------------------------------------------
// RPC 67: SetArmedWeapon
// Payload (wire order):
//   uint32 weaponId - Selected in-game weapon ID.
void RPCHandlers::SetArmedWeapon(RPCParameters* pParams)
{
    if (!pParams || !pParams->input)
        return;

    BitStream bs(pParams->input, BITS_TO_BYTES(pParams->numberOfBitsOfData), false);

    std::uint32_t weaponId{};

    if (!bs.Read(weaponId))
        return;

    if (weaponId <= 46)
        if (auto* ped = g_game ? g_game->GetPlayerPed() : nullptr)
            ped->SetArmedWeapon(static_cast<int>(weaponId));
}

// -----------------------------------------------------------------------------
// RPC 113: SetPlayerAttachedObject
// Payload (wire order):
//   uint16 playerId - Target player ID.
//   uint32 index - Attached-object slot.
//   bool create - True creates/updates the attachment; false removes it.
//   int32 modelId - Object model ID; DL permits custom model IDs.
//   uint32 bone - Ped bone ID.
//   float offsetX - Offset X.
//   float offsetY - Offset Y.
//   float offsetZ - Offset Z.
//   float rotX - Rotation X.
//   float rotY - Rotation Y.
//   float rotZ - Rotation Z.
//   float scaleX - Scale X.
//   float scaleY - Scale Y.
//   float scaleZ - Scale Z.
//   uint32 materialColor1 - Material color 1.
//   uint32 materialColor2 - Material color 2.
// Note: If create is false, the payload ends after the first three fields.
// Note: DL-specific modelId is signed int32.
void RPCHandlers::SetPlayerAttachedObject(RPCParameters* pParams)
{
    auto* players = g_netGame ? g_netGame->GetPlayerPool() : nullptr;
    if (!players || !pParams || !pParams->input)
        return;

    BitStream bs(pParams->input, BITS_TO_BYTES(pParams->numberOfBitsOfData), false);
    std::uint16_t playerId{};
    std::uint32_t index{};
    bool create{};
    if (!bs.Read(playerId) || !bs.Read(index) || !bs.Read(create) || playerId >= CPlayerPool::MAX_PLAYERS || index >= CPed::MAX_ACCESSORIES)
        return;

    CPed* ped = nullptr;
    if (playerId == players->m_localInfo.m_nId)
    {
        if (auto* local = players->GetLocalPlayer())
            ped = local->GetPed();
    }
    else if (auto* remote = players->GetPlayer(playerId))
    {
        ped = remote->m_pPed;
    }
    if (!ped || !ped->IsValid())
        return;

    if (!create)
    {
        ped->DeleteAccessory(static_cast<int>(index));
        return;
    }

    CPed::Accessory info{};
    if (!bs.Read(info.m_nModel) || !bs.Read(info.m_nBone) || !bs.Read(info.m_offset.x) || !bs.Read(info.m_offset.y) || !bs.Read(info.m_offset.z) || !bs.Read(info.m_rotation.x) ||
        !bs.Read(info.m_rotation.y) || !bs.Read(info.m_rotation.z) || !bs.Read(info.m_scale.x) || !bs.Read(info.m_scale.y) || !bs.Read(info.m_scale.z) ||
        !bs.Read(info.m_firstMaterialColor) || !bs.Read(info.m_secondMaterialColor))
        return;

    ped->AddAccessory(static_cast<int>(index), &info);
}

// -----------------------------------------------------------------------------
// RPC 41: PlayAudioStream
// Payload (wire order):
//   uint8 urlLength - URL length.
//   char[urlLength] url - Audio-stream URL.
//   float x - World X.
//   float y - World Y.
//   float z - World Z.
//   float radius - Audible radius.
//   uint8 usePosition - Nonzero enables positional playback.
void RPCHandlers::PlayAudioStream(RPCParameters* pParams)
{
    if (!pParams || !pParams->input)
        return;

    BitStream bs(pParams->input, BITS_TO_BYTES(pParams->numberOfBitsOfData), false);

    std::uint8_t urlLength{};
    std::string url{};
    float x{};
    float y{};
    float z{};
    float radius{};
    std::uint8_t usePosition{};

    if (!bs.Read(urlLength) || bs.GetNumberOfUnreadBits() < urlLength * 8u)
        return;
    url.resize(urlLength);
    if (urlLength && !bs.Read(url.data(), urlLength))
        return;
    if (!bs.Read(x) || !bs.Read(y) || !bs.Read(z) || !bs.Read(radius) || !bs.Read(usePosition))
        return;

    if (g_audioStream)
        g_audioStream->Play(url.c_str(), CVector{x, y, z}, radius, usePosition != 0);
}

// -----------------------------------------------------------------------------
// RPC 42: StopAudioStream
// Payload (wire order):
// Note: No payload fields.
void RPCHandlers::StopAudioStream(RPCParameters* pParams)
{
    if (!pParams)
        return;

    BitStream bs;
    (void)bs;

    if (g_audioStream)
        g_audioStream->Stop(false);
}

// -----------------------------------------------------------------------------
// RPC 43: RemoveBuilding
// Payload (wire order):
//   uint32 modelId - Building model ID.
//   float x - Center X.
//   float y - Center Y.
//   float z - Center Z.
//   float radius - Removal radius.
void RPCHandlers::RemoveBuilding(RPCParameters* pParams)
{
    if (!pParams || !pParams->input)
        return;

    BitStream bs(pParams->input, BITS_TO_BYTES(pParams->numberOfBitsOfData), false);

    std::int32_t modelId{};
    float x{};
    float y{};
    float z{};
    float radius{};

    if (!bs.Read(modelId))
        return;
    if (!bs.Read(x))
        return;
    if (!bs.Read(y))
        return;
    if (!bs.Read(z))
        return;
    if (!bs.Read(radius))
        return;

    BuildingRemoval::RemoveBuildingsInArea(modelId, x, y, z, radius);
}

// -----------------------------------------------------------------------------
// RPC 81: AttachCameraToObject
// Payload (wire order):
//   uint16 objectId - Object ID to attach the camera to.
void RPCHandlers::AttachCameraToObject(RPCParameters* pParams)
{
    if (!pParams || !pParams->input)
        return;

    BitStream bs(pParams->input, BITS_TO_BYTES(pParams->numberOfBitsOfData), false);

    std::uint16_t objectId{};

    if (!bs.Read(objectId))
        return;

    if (!g_game || !g_netGame)
        return;

    auto* pool = g_netGame->GetObjectPool();
    auto* object = pool ? pool->Get(objectId) : nullptr;
    auto* camera = g_game->GetCamera();
    if (!object || !camera)
        return;

    if (auto* players = g_netGame->GetPlayerPool())
        if (auto* local = players->GetLocalPlayer())
            local->m_spectating.m_bProcessed = TRUE;

    camera->Attach(object);
}

// -----------------------------------------------------------------------------
// RPC 82: InterpolateCamera
// Payload (wire order):
//   bool setPosition - True interpolates camera position; false interpolates look-at.
//   float fromX - Start X.
//   float fromY - Start Y.
//   float fromZ - Start Z.
//   float toX - Destination X.
//   float toY - Destination Y.
//   float toZ - Destination Z.
//   uint32 time - Interpolation duration.
//   uint8 cutType - Interpolation/cut mode.
void RPCHandlers::InterpolateCamera(RPCParameters* pParams)
{
    if (!pParams || !pParams->input)
        return;

    BitStream bs(pParams->input, BITS_TO_BYTES(pParams->numberOfBitsOfData), false);

    bool setPosition{};
    float fromX{};
    float fromY{};
    float fromZ{};
    float toX{};
    float toY{};
    float toZ{};
    std::int32_t time{};
    std::uint8_t cutType{};

    if (!bs.Read(setPosition))
        return;
    if (!bs.Read(fromX))
        return;
    if (!bs.Read(fromY))
        return;
    if (!bs.Read(fromZ))
        return;
    if (!bs.Read(toX))
        return;
    if (!bs.Read(toY))
        return;
    if (!bs.Read(toZ))
        return;
    if (!bs.Read(time))
        return;
    if (!bs.Read(cutType))
        return;

    if (time <= 0 || !g_game)
        return;

    if (cutType != 1 && cutType != 2)
        cutType = 2;

    auto* camera = g_game->GetCamera();
    if (!camera)
        return;

    if (auto* players = g_netGame ? g_netGame->GetPlayerPool() : nullptr)
        if (auto* local = players->GetLocalPlayer())
            local->m_spectating.m_bProcessed = TRUE;

    const CVector from{fromX, fromY, fromZ};
    const CVector to{toX, toY, toZ};

    if (setPosition)
        camera->SetMoveVector(&from, &to, time, cutType);
    else
        camera->SetTrackVector(&from, &to, time, cutType);
}

// -----------------------------------------------------------------------------
// RPC 83: ToggleSelectTextDraw
// Payload (wire order):
//   bool enabled - One bit enables TextDraw selection.
//   uint32 hoverColor - Hover RGBA color.
void RPCHandlers::ToggleSelectTextDraw(RPCParameters* pParams)
{
    if (!pParams || !pParams->input)
        return;

    BitStream bs(pParams->input, BITS_TO_BYTES(pParams->numberOfBitsOfData), false);

    bool enabled{};
    std::uint32_t hoverColor{};

    if (!bs.Read(enabled))
        return;
    if (!bs.Read(hoverColor))
        return;

    if (g_textDrawSelection)
    {
        if (enabled)
            g_textDrawSelection->Enable(hoverColor);
        else
            g_textDrawSelection->Disable(false);
    }
}

// -----------------------------------------------------------------------------
// RPC 84: SetPlayerObjectMaterial(Text)
// Payload (wire order):
// Note: Variant is selected by materialType after objectId.
// Note: Material variant wire order: uint16 objectId, uint8 materialType, uint8 materialIndex, uint16 modelId, uint8 libraryLen, char[libraryLen] library, uint8 textureLen, char[textureLen] texture, uint32 color.
// Note: Material-text variant wire order: uint16 objectId, uint8 materialType, uint8 materialIndex, uint8 materialSize, uint8 fontNameLen, char[fontNameLen] fontName, uint8 fontSize, uint8 bold, uint32 fontColor, uint32 backgroundColor, uint8 align, CSTRING[2048] text.
void RPCHandlers::SetPlayerObjectMaterial(RPCParameters* pParams)
{
    if (!pParams || !pParams->input)
        return;
    BitStream bs(pParams->input, BITS_TO_BYTES(pParams->numberOfBitsOfData), false);
    std::uint16_t objectId{};
    if (!bs.Read(objectId))
        return;
    ObjectMaterialData material{};
    if (!ReadObjectMaterial(bs, material))
        return;
    ApplyObjectMaterial(NetworkObject(objectId), material);
}

// -----------------------------------------------------------------------------
// RPC 169: SetPlayerObjectNoCameraCol
// Payload (wire order):
//   uint16 objectId - Object ID whose camera collision is disabled.
// Note: Not listed on the wiki; uint16 payload confirmed from the DL DLL.
// void RPCHandlers::SetPlayerObjectNoCameraCol(RPCParameters* pParams)
// {
//     std::uint16_t objectId{};

//     (void)pParams;
// }

// -----------------------------------------------------------------------------
// RPC 173: ApplyActorAnimation
// Payload (wire order):
//   uint16 actorId - Actor ID.
//   uint8 animLibLength - Animation library name length.
//   char[animLibLength] animLib - Animation library name.
//   uint8 animNameLength - Animation name length.
//   char[animNameLength] animName - Animation name.
//   float delta - Animation blend/frame delta.
//   bool loop - Loop flag.
//   bool lockX - Lock X flag.
//   bool lockY - Lock Y flag.
//   bool freeze - Freeze-at-end flag.
//   uint32 time - Animation duration/time.
void RPCHandlers::ApplyActorAnimation(RPCParameters* pParams)
{
    if (!pParams || !pParams->input)
        return;

    BitStream bs(pParams->input, BITS_TO_BYTES(pParams->numberOfBitsOfData), false);

    std::uint16_t actorId{};
    std::uint8_t animLibLength{};
    std::string animLib{};
    std::uint8_t animNameLength{};
    std::string animName{};
    float delta{};
    bool loop{};
    bool lockX{};
    bool lockY{};
    bool freeze{};
    std::uint32_t time{};

    if (!bs.Read(actorId) || !bs.Read(animLibLength) || bs.GetNumberOfUnreadBits() < animLibLength * 8u)
        return;
    animLib.resize(animLibLength);
    if ((animLibLength && !bs.Read(animLib.data(), animLibLength)) || !bs.Read(animNameLength) || bs.GetNumberOfUnreadBits() < animNameLength * 8u)
        return;
    animName.resize(animNameLength);
    if (animNameLength && !bs.Read(animName.data(), animNameLength))
        return;
    if (!bs.Read(delta) || !bs.Read(loop) || !bs.Read(lockX) || !bs.Read(lockY) || !bs.Read(freeze) || !bs.Read(time))
        return;

    auto* pool = g_netGame ? g_netGame->GetActorPool() : nullptr;
    if (!pool || animLib.empty() || animName.empty())
        return;

    if (auto* actor = pool->Get(actorId))
        actor->PerformAnimation(animName.c_str(), animLib.c_str(), delta, static_cast<int>(loop), static_cast<int>(lockX), static_cast<int>(lockY), static_cast<int>(freeze),
                                static_cast<int>(time));
}

// -----------------------------------------------------------------------------
// RPC 174: ClearActorAnimation
// Payload (wire order):
//   uint16 actorId - Actor ID.
void RPCHandlers::ClearActorAnimation(RPCParameters* pParams)
{
    if (!pParams || !pParams->input)
        return;

    BitStream bs(pParams->input, BITS_TO_BYTES(pParams->numberOfBitsOfData), false);

    std::uint16_t actorId{};

    if (!bs.Read(actorId))
        return;

    if (auto* pool = g_netGame ? g_netGame->GetActorPool() : nullptr)
        if (auto* actor = pool->Get(actorId))
            actor->ClearAnimations();
}

// -----------------------------------------------------------------------------
// RPC 175: SetActorFacingAngle
// Payload (wire order):
//   uint16 actorId - Actor ID.
//   float angle - Actor facing angle.
void RPCHandlers::SetActorFacingAngle(RPCParameters* pParams)
{
    if (!pParams || !pParams->input)
        return;

    BitStream bs(pParams->input, BITS_TO_BYTES(pParams->numberOfBitsOfData), false);

    std::uint16_t actorId{};
    float angle{};

    if (!bs.Read(actorId))
        return;
    if (!bs.Read(angle))
        return;

    if (auto* pool = g_netGame ? g_netGame->GetActorPool() : nullptr)
        if (auto* actor = pool->Get(actorId))
            actor->SetRotation(angle);
}

// -----------------------------------------------------------------------------
// RPC 176: SetActorPos
// Payload (wire order):
//   uint16 actorId - Actor ID.
//   float x - X.
//   float y - Y.
//   float z - Z.
void RPCHandlers::SetActorPos(RPCParameters* pParams)
{
    if (!pParams || !pParams->input)
        return;

    BitStream bs(pParams->input, BITS_TO_BYTES(pParams->numberOfBitsOfData), false);

    std::uint16_t actorId{};
    float x{};
    float y{};
    float z{};

    if (!bs.Read(actorId))
        return;
    if (!bs.Read(x))
        return;
    if (!bs.Read(y))
        return;
    if (!bs.Read(z))
        return;

    if (auto* pool = g_netGame ? g_netGame->GetActorPool() : nullptr)
        if (auto* actor = pool->Get(actorId))
            actor->Teleport(CVector{x, y, z});
}

// -----------------------------------------------------------------------------
// RPC 178: SetActorHealth
// Payload (wire order):
//   uint16 actorId - Actor ID.
//   float health - Actor health.
void RPCHandlers::SetActorHealth(RPCParameters* pParams)
{
    if (!pParams || !pParams->input)
        return;

    BitStream bs(pParams->input, BITS_TO_BYTES(pParams->numberOfBitsOfData), false);

    std::uint16_t actorId{};
    float health{};

    if (!bs.Read(actorId))
        return;
    if (!bs.Read(health))
        return;

    if (auto* pool = g_netGame ? g_netGame->GetActorPool() : nullptr)
        if (auto* actor = pool->Get(actorId))
            actor->SetHealth(health);
}

// -----------------------------------------------------------------------------
// RPC 76: InitMenu
// menu strings are fixed 32-byte buffers, not length-prefixed strings
void RPCHandlers::InitMenu(RPCParameters* pParams)
{
    auto* pool = g_netGame ? g_netGame->GetMenuPool() : nullptr;
    if (!pool || !pParams || !pParams->input)
        return;
    BitStream bs(pParams->input, BITS_TO_BYTES(pParams->numberOfBitsOfData), false);
    std::uint8_t id{};
    std::uint32_t twoColumns{};
    char title[CMenu::MAX_MENU_LINE]{};
    float x{}, y{}, width1{}, width2{};
    CMenu::Interaction interaction{};
    char headers[2][CMenu::MAX_MENU_LINE]{};
    char items[2][CMenu::MAX_MENU_ITEMS][CMenu::MAX_MENU_LINE]{};
    std::uint8_t counts[2]{};
    constexpr int wireLength = CMenu::MAX_MENU_LINE - 1;
    if (!bs.Read(id) || id >= CMenuPool::MAX_MENUS || !bs.Read(twoColumns) || twoColumns > 1 || !bs.Read(title, wireLength) || !bs.Read(x) || !bs.Read(y) || !bs.Read(width1))
        return;
    if (twoColumns && !bs.Read(width2))
        return;
    if (!bs.Read(interaction.m_bMenu))
        return;
    for (auto& row : interaction.m_bRow)
        if (!bs.Read(row))
            return;
    for (unsigned column = 0; column < twoColumns + 1; ++column)
    {
        if (!bs.Read(headers[column], wireLength) || !bs.Read(counts[column]) || counts[column] > CMenu::MAX_MENU_ITEMS)
            return;
        for (unsigned row = 0; row < counts[column]; ++row)
            if (!bs.Read(items[column][row], wireLength))
                return;
    }
    auto* menu = pool->Create(id, x, y, static_cast<char>(twoColumns + 1), width1, width2, &interaction);
    if (!menu)
        return;
    menu->SetTitle(title);
    for (unsigned column = 0; column < twoColumns + 1; ++column)
    {
        menu->SetColumnTitle(column, headers[column]);
        for (unsigned row = 0; row < counts[column]; ++row)
            menu->AddItem(column, row, items[column][row]);
    }
}

// -----------------------------------------------------------------------------
// RPC 77: ShowMenu
// Payload (wire order):
//   uint8 menuId - Menu ID to show.
void RPCHandlers::ShowMenu(RPCParameters* pParams)
{
    if (!pParams || !pParams->input)
        return;

    BitStream bs(pParams->input, BITS_TO_BYTES(pParams->numberOfBitsOfData), false);

    std::uint8_t menuId{};

    if (!bs.Read(menuId))
        return;

    if (auto* pool = g_netGame ? g_netGame->GetMenuPool() : nullptr)
        pool->Show(menuId);
}

// -----------------------------------------------------------------------------
// RPC 78: HideMenu
// Payload (wire order):
//   uint8 menuId - Menu ID to hide.
void RPCHandlers::HideMenu(RPCParameters* pParams)
{
    if (!pParams || !pParams->input)
        return;

    BitStream bs(pParams->input, BITS_TO_BYTES(pParams->numberOfBitsOfData), false);

    std::uint8_t menuId{};

    if (!bs.Read(menuId))
        return;

    if (auto* pool = g_netGame ? g_netGame->GetMenuPool() : nullptr)
        pool->Hide(menuId);
}

// 0.3.DL-only RPC 48. Only informs the download subsystem about the virtual world.
void RPCHandlers::SetPlayerVirtualWorld(RPCParameters* pParams)
{
    if (!g_DLClient || !pParams || !pParams->input)
        return;
    BitStream bs(pParams->input, BITS_TO_BYTES(pParams->numberOfBitsOfData), false);
    std::int32_t virtualWorld{};
    if (!bs.Read(virtualWorld))
        return;
    // TODO: update DL downloads for this virtual world.
}

// 0.3.DL-only RPC 179: model metadata, in wire order.
void RPCHandlers::ModelInfo(RPCParameters* pParams)
{
    if (!g_DLClient || !pParams || !pParams->input)
        return;
    BitStream bs(pParams->input, BITS_TO_BYTES(pParams->numberOfBitsOfData), false);
    std::uint32_t index{}, dffCrc{}, txdCrc{}, dffSize{}, txdSize{};
    std::int32_t count{}, virtualWorld{}, baseId{}, newId{};
    std::uint8_t type{}, timeOn{}, timeOff{};
    if (!bs.Read(index) || !bs.Read(count) || !bs.Read(type) || !bs.Read(virtualWorld) || !bs.Read(baseId) || !bs.Read(newId) || !bs.Read(dffCrc) || !bs.Read(txdCrc) ||
        !bs.Read(dffSize) || !bs.Read(txdSize) || !bs.Read(timeOn) || !bs.Read(timeOff))
        return;
    // TODO: queue metadata in CCustomModelsPool.
}

// 0.3.DL-only RPC 183: variant 1 binary file, variant 6 HTTP URL.
void RPCHandlers::ModelFile(RPCParameters* pParams)
{
    if (!g_DLClient || !pParams || !pParams->input)
        return;
    BitStream bs(pParams->input, BITS_TO_BYTES(pParams->numberOfBitsOfData), false);
    std::uint8_t arg{}, fileType{};
    std::uint32_t crc{};
    if (!bs.Read(arg))
        return;
    if (arg == 1)
    {
        std::uint32_t length{};
        if (!bs.Read(fileType) || !bs.Read(crc) || !bs.Read(length) || length > 64u * 1024u * 1024u || bs.GetNumberOfUnreadBits() < static_cast<std::uint64_t>(length) * 8u)
            return;
        std::vector<std::uint8_t> data(length);
        if (length && !bs.Read(reinterpret_cast<char*>(data.data()), static_cast<int>(length)))
            return;
        // TODO: validate CRC and submit file to the download pool.
    }
    else if (arg == 6)
    {
        std::uint8_t length{};
        if (!bs.Read(fileType) || !bs.Read(crc) || !bs.Read(length) || bs.GetNumberOfUnreadBits() < length * 8u)
            return;
        std::string url(length, '\0');
        if (length && !bs.Read(url.data(), length))
            return;
        // TODO: submit URL to CHttpClient after URL validation.
    }
    // TODO: other undocumented ModelFile variants.
}

// 0.3.DL-only RPC 185: no payload, download state ended.
void RPCHandlers::SrcFinishedDownloading(RPCParameters* pParams)
{
    if (!g_DLClient || !pParams)
        return;
    BitStream bs;
    (void)bs;
    // TODO: leave the DL downloading state.
}
