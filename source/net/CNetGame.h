#pragma once

#include <cstdint>

#include "pools/CActorPool.h"
#include "pools/CGangZonePool.h"
#include "pools/CLabelPool.h"
#include "pools/CMenuPool.h"
#include "pools/CObjectPool.h"
#include "pools/CPickupPool.h"
#include "pools/CPlayerPool.h"
#include "pools/CTextDrawPool.h"
#include "pools/CVehiclePool.h"
#include "sync/Synchronization.h"
#include "utils/math/CVector.h"

struct Packet;
class RakClientInterface;

enum MarkersMode
{
    MARKERS_MODE_OFF,
    MARKERS_MODE_GLOBAL,
    MARKERS_MODE_STREAMED
};

class CNetGame
{
public:
    using ID = CPlayerPool::ID;

    enum GameMode
    {
        GAME_MODE_WAITCONNECT = 1,
        GAME_MODE_CONNECTING = 2,
        GAME_MODE_CONNECTED = 5,
        GAME_MODE_WAITJOIN = 6,
        GAME_MODE_RESTARTING = 11
    };

    enum class PickupSendMode
    {
        Notification,
        Process
    };

    enum
    {
        NETMODE_STATS_UPDATE_DELAY = 1000,
        NETMODE_INCAR_SENDRATE = 30,
        NETMODE_ONFOOT_SENDRATE = 30,
        NETMODE_FIRING_SENDRATE = 30,
        LANMODE_INCAR_SENDRATE = 15,
        LANMODE_ONFOOT_SENDRATE = 15,
        NETMODE_SEND_MULTIPLIER = 2,
        NETMODE_AIM_UPDATE_DELAY = 500,
        NETMODE_HEAD_UPDATE_DELAY = 1000,
        NETMODE_TARGET_UPDATE_DELAY = 100,
        NETMODE_PLAYERS_UPDATE_DELAY = 3000
    };

    struct Pools
    {
        CVehiclePool* m_pVehicle;   // 0x00
        CPlayerPool* m_pPlayer;     // 0x04
        CPickupPool* m_pPickup;     // 0x08
        CObjectPool* m_pObject;     // 0x0C
        CActorPool* m_pActor;       // 0x10
        CGangZonePool* m_pGangZone; // 0x14
        CLabelPool* m_pLabel;       // 0x18
        CTextDrawPool* m_pTextDraw; // 0x1C
        CMenuPool* m_pMenu;         // 0x20
    };

    struct Settings
    {
        bool m_bUseCJWalk;                   // 0x00
        unsigned int m_nDeadDropsMoney;      // 0x01
        float m_fWorldBoundaries[4];         // 0x05
        bool m_bAllowWeapons;                // 0x15
        float m_fGravity;                    // 0x16
        bool m_bEnterExit;                   // 0x1A
        BOOL m_bVehicleFriendlyFire;         // 0x1B
        bool m_bHoldTime;                    // 0x1F
        bool m_bInstagib;                    // 0x20
        bool m_bZoneNames;                   // 0x21
        bool m_bFriendlyFire;                // 0x22
        BOOL m_bClassesAvailable;            // 0x23
        float m_fNameTagsDrawDist;           // 0x27
        bool m_bManualVehicleEngineAndLight; // 0x2B
        unsigned char m_nWorldTimeHour;      // 0x2C
        unsigned char m_nWorldTimeMinute;    // 0x2D
        unsigned char m_nWeather;            // 0x2E
        bool m_bNoNametagsBehindWalls;       // 0x2F
        int m_nPlayerMarkersMode;            // 0x30
        float m_fChatRadius;                 // 0x34
        bool m_bNameTags;                    // 0x38
        bool m_bLtdChatRadius;               // 0x39
    };

    RakClientInterface* m_pRakClient;   // 0x000
    char pad_0[44];                     // 0x004
    char m_szHostAddress[257];          // 0x030
    char m_szHostname[257];             // 0x131
    bool m_bDisableCollision;           // 0x232
    bool m_bUpdateCameraTarget;         // 0x233
    bool m_bNametagStatus;              // 0x234
    int m_nPort;                        // 0x235
    BOOL m_bLanMode;                    // 0x239
    std::uint32_t m_aMapIcons[100];     // 0x23D
    int m_nGameState;                   // 0x3CD
    std::uint32_t m_lastConnectAttempt; // 0x3D1
    Settings* m_pSettings;              // 0x3D5
    char pad_2[5];                      // 0x3D9
    Pools* m_pPools;                    // 0x3DE

    static std::uint32_t& RefLastPlayersUpdateRequest();
    static std::uint32_t& RefLagCompMode();

    CNetGame(const char* hostAddress, int port, const char* nick, const char* password);
    ~CNetGame();

    CNetGame(const CNetGame&) = delete;
    CNetGame& operator=(const CNetGame&) = delete;

    CPickupPool* GetPickupPool();
    CMenuPool* GetMenuPool();
    void SetState(int value);
    void InitializePools();
    void InitialNotification();
    void InitializeGameLogic();
    void Connect();
    void Disconnect();
    void SpawnScreen();

    void RegisterRPC();
    void UnRegisterRPC();

    void Packet_RSAPublicKeyMismatch(Packet* packet);
    void Packet_ConnectionBanned(Packet* packet);
    void Packet_ConnectionRequestAcepted(Packet* packet);
    void Packet_NoFreeIncomingConnections(Packet* packet);
    void Packet_DisconnectionNotification(Packet* packet);
    void Packet_InvalidPassword(Packet* packet);
    void Packet_ConnectionAttemptFailed(Packet* packet);

    void UpdatePlayers();
    void DeleteMarker(std::uint8_t index);
    void ResetPlayerPool();
    void ResetVehiclePool();
    void ResetTextDrawPool();
    void ResetObjectPool();
    void ResetGangZonePool();
    void ResetPickupPool();
    void ResetMenuPool();
    void ResetLabelPool();
    void ResetActorPool();

    void Packet_UnoccupiedSync(Packet* packet);
    void Packet_BulletSync(Packet* packet);
    void Packet_AimSync(Packet* packet);
    void Packet_PassengerSync(Packet* packet);
    void Packet_TrailerSync(Packet* packet);
    void Packet_MarkersSync(Packet* packet);
    void Packet_AuthKey(Packet* packet);

    void ResetMarkers();
    void CreateMarker(std::uint8_t index, CVector position, char icon, int color, int type);
    void ResetPools();
    void ShutdownForRestart();

    void Packet_PlayerSync(Packet* packet);
    void Packet_VehicleSync(Packet* packet);
    void Packet_ConnectionLost(Packet* packet);
    void Packet_ConnectionSucceeded(Packet* packet);

    void UpdateNetwork();
    void Process();
    void ProcessGameStuff();
    void SendRconCommand(const char* command);

    bool EnterVehicle(std::uint16_t vehicleId, std::uint8_t isPassenger);
    bool ExitVehicle(std::uint16_t vehicleId);
    bool VehicleDamaged(std::uint16_t vehicleId, std::uint32_t panels, std::uint32_t doors, std::uint8_t lights, std::uint8_t tires);
    bool ScmEvent(std::uint32_t vehicleId, std::uint32_t param1, std::uint32_t param2, std::uint32_t eventType);
    bool VehicleDestroyed(std::uint16_t vehicleId);
    bool SendSpawn();
    bool ChatMessage(const char* message);
    bool InteriorChangeNotification(std::uint8_t interiorId);
    bool DeathNotification(std::uint8_t reason, std::uint16_t killerId);
    bool SendCommand(const char* text);
    bool ClickPlayer(std::uint16_t playerId, std::uint8_t source);
    bool DialogResponse(std::uint16_t dialogId, bool response, std::int16_t listItem, const char* inputText);
    bool ClientCheckResponse(std::uint8_t type, std::uint32_t address, std::uint8_t response);
    bool GiveTakeDamage(bool giveOrTake, std::uint16_t playerId, float damage, std::uint32_t weaponId, std::uint32_t bodyPart);
    bool GiveActorDamage(bool unused, std::uint16_t actorId, float damage, std::uint32_t weaponId, std::uint32_t bodyPart);
    bool MapMarker(float x, float y, float z);
    std::uint32_t m_nClassesAvailable = 0;
    bool RequestClass(std::int32_t classId);
    bool RequestSpawn();
    bool MenuSelect(std::uint8_t row);
    bool MenuQuit();
    bool SelectTextDraw(std::uint16_t textDrawId);
    bool PickedUpPickup(std::int32_t pickupId, PickupSendMode mode);
    bool SelectObject(std::uint32_t selectType, std::uint16_t objectId, std::uint32_t modelId, float x, float y, float z);
    bool EditAttachedObject(std::uint32_t response, std::uint32_t index, std::uint32_t modelId, std::uint32_t boneId, const CVector& offset, const CVector& rotation,
                            const CVector& scale, std::uint32_t color1, std::uint32_t color2);
    bool EditObject(bool playerObject, std::uint16_t objectId, std::uint32_t response, const CVector& position, const CVector& rotation);
    bool UpdateScoresAndPings();
    bool ClientJoin(std::uint32_t version, std::uint8_t mod, const char* nickname, std::uint32_t challengeResponse, const char* authKey, const char* clientVersion);
    bool NPCJoin(std::uint32_t version, std::uint8_t mod, const char* nickname, std::uint32_t challengeResponse);
    bool CameraTarget(std::uint16_t objectTarget, std::uint16_t vehicleTarget, std::uint16_t playerTarget, std::uint16_t actorTarget);

    bool FinishedDownloading(std::uint8_t unused = 128); // 0.3.DL, RPC 184
    bool RequestDffFile(std::uint32_t crc);              // 0.3.DL, RPC 181
    bool RequestTxdFile(std::uint32_t crc);              // 0.3.DL, RPC 182

    bool SrvNetStats();                              // legacy
    bool DestroyWeaponPickup(std::uint16_t exOwner); // legacy

    CPlayerPool* GetPlayerPool();
    CObjectPool* GetObjectPool();
    CActorPool* GetActorPool();
    int GetState();
    BOOL LanMode();
    CVehiclePool* GetVehiclePool();
    RakClientInterface* GetRakClient();
    __int64 GetCounter();
};
