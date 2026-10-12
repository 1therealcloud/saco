#pragma once

#include <Windows.h>
#include <d3d9.h>

#include <cstddef>
#include <cstdint>

#include "utils/math/CVector.h"
#include "Cursor.h"

class CAudio;
class CCamera;
class CPed;
class CVehicle;
class CObject;
class CWeaponInfo;
class CPad;

using GTAREF = std::uint32_t;

class CGame
{
public:
    CAudio* m_pAudio;
    CCamera* m_pCamera;
    CPed* m_pPlayerPed;

    struct RacingCheckpoint
    {
        CVector m_currentPosition;
        CVector m_nextPosition;
        float m_fSize;
        char m_nType;
        BOOL m_bEnabled;
        GTAREF m_marker;
        GTAREF m_handle;
    } m_racingCheckpoint{};

    struct Checkpoint
    {
        CVector m_position;
        CVector m_size;
        BOOL m_bEnabled;
        GTAREF m_handle;
    } m_checkpoint{};

    int field_61;
    BOOL m_bHeadMove;
    int m_nFrameLimiter;
    int m_nCursorMode;
    unsigned int m_nInputEnableWaitFrames;
    BOOL m_bClockEnabled;
    char field_6d;
    bool m_aKeepLoadedVehicleModels[212];

    static std::uint8_t GetState();
    static void SetState(std::uint8_t value);
    static bool IsPlaying();
    static bool IsPaused();
    static void* TraceObjectRay(const CVector& origin, const CVector& target);
    static int ScreenWidth();
    static int ScreenHeight();
    static float FrameSeconds();
    static float HudScaleX();
    static float HudScaleY();
    static bool HasFontResources();
    static void ExpandControlKeys(char* text);
    static void FlushFonts();
    static std::uint8_t GetHudState();
    static std::uint8_t GetRadarState();
    static void SetHudState(std::uint8_t value);
    static void SetRadarState(std::uint8_t value);

    static BOOL* ArrayUsedPlayerSlots(); // 0x150340

    CGame(); // 0x9FF80
    ~CGame();
    CGame(const CGame&) = delete;
    CGame& operator=(const CGame&) = delete;

    CPed* GetPlayerPed();                                     // 0x1010
    float FindGroundZ(CVector point);                         // 0xA0400
    void SetCursorMode(int mode, BOOL immediatelyHideCursor); // 0xA06F0
    bool InitGame();                                          // 0xA0890
    bool StartGame();                                         // 0xA08E0
    void OnGameplayReady();
    BOOL IsMenuVisible();                                // 0xA0920
    BOOL IsStarted();                                    // 0xA0930
    void RequestModel(int model, int loadingStream = 0); // 0xA0940
    void LoadRequestedModels();                          // 0xA0960
    BOOL IsModelAvailable(int model);                    // 0xA0970
    void ReleaseModel(int model, bool gameFunc = true);  // 0xA09A0
    void SetWorldTime(char hour, char minute);           // 0xA0AB0
    void GetWorldTime(char* hour, char* minute);         // 0xA0AE0
    void LetTimeGo(bool let);                            // 0xA0B00
    void SetWorldWeather(char weather);                  // 0xA0B40
    void CreateExplosion(CVector position, int type, float radius);
    void SetFrameLimiter(int value);                                      // 0xA0BB0
    void SetMaxStats();                                                   // 0xA0BE0
    void DisableTrainTraffic();                                           // 0xA0C10
    void RefreshRenderer(float x, float y);                               // 0xA0C20
    void RequestAnimation(const char* file);                              // 0xA0C50
    BOOL IsAnimationLoaded(const char* file);                             // 0xA0C70
    void ReleaseAnimation(const char* file);                              // 0xA0C90
    void DisplayGameText(const char* text, int type, int size);           // 0xA0CE0
    void DeleteRacingCheckpoint();                                        // 0xA0D60
    GTAREF CreateMarker(int icon, CVector position, int color, int type); // 0xA0D90
    bool SetMarkerPosition(GTAREF handle, const CVector& position);
    void DeleteMarker(GTAREF handle);        // 0xA0EC0
    char GetCurrentInterior();               // 0xA0EE0
    void UpdateFarClippingPlane();           // 0xA0F00
    void IncreasePlayerMoney(int increment); // 0xA0F70
    int GetPlayerMoney();                    // 0xA0F90
    void PlayCrimeReport(int crime, const CVector* position, int inVehicle, int vehicleModel, int vehicleColor);

    const char* GetWeaponName(int weapon); // 0xA0FA0
    // Creates a one-shot weapon pickup (opcode 0x032B) and returns the raw GTA pickup reference, 0 on failure.
    GTAREF CreateWeaponPickup(int model, int ammo, CVector position); // 0xA12D0
    ::CWeaponInfo* GetWeaponInfo(int weapon, int skill);              // 0xA13E0

    GTAREF CreatePickup(int model, int type, CVector position, GTAREF* handle); // 0xA11F0
    IDirect3DDevice9* GetDevice();                                              // 0xA1370
    void Restart();                                                             // 0xA13B0
    void SetWorldGravity(float value);                                          // 0xA1400
    void SetWantedLevel(char level);                                            // 0xA1420
    void SetNumberOfIntroTextLinesThisFrame(unsigned short value);              // 0xA1430
    void DrawGangZone(float* position, std::uint32_t color);                    // 0xA1440
    void EnableZoneDisplaying(bool enable);                                     // 0xA1520
    void EnableStuntBonus(bool enable);                                         // 0xA1540
    void LoadScene(const char* file);                                           // 0xA15B0
    int GetUsedMemory();                                                        // 0xA15D0
    int GetStreamingMemory();                                                   // 0xA15E0
    void SetRequiredVehicleModels(unsigned char* modelCount);                   // 0xA1610
    int GetTimer();                                                             // 0xA1770
    void SetTimer(int value);                                                   // 0xA1750
    void SetMapIcon(std::uint8_t id, CVector position, int icon, int color, int type);
    void RemoveMapIcon(std::uint8_t id);
    void ResetMapIcons();
    static bool GetMapClickPosition(CVector& position);
    void LoadAnimationsAndModels();                       // 0xA18A0
    void LoadCollisionFile(const char* file);             // 0xA1B80
    void LoadCullZone(const char* line);                  // 0xA1BA0
    BOOL UsingGamepad();                                  // 0xA1BC0
    void DisableAutoAiming();                             // 0xA1BD0
    void EnableHUD(BOOL enable);                          // 0xA1DB0
    void SetCheckpoint(CVector* position, CVector* size); // 0xA1DE0
    void DisableCheckpoint();
    void CreateRacingCheckpoint();                                                                   // 0xA1EA0
    void ProcessCheckpoints();                                                                       // 0xA1F20
    void ResetMoney();                                                                               // 0xA20C0
    void SetRacingCheckpoint(int type, CVector* currentPosition, CVector* nextPosition, float size); // 0xA2100
    void EnableRadar(BOOL enable);                                                                   // 0xA0CC0
    void* GetWindowHandle();                                                                         // 0x2D10
    CAudio* GetAudio();                                                                              // 0x2D20
    CCamera* GetCamera();                                                                            // 0x2D30
    BOOL DoesHeadMoves();                                                                            // 0x2D40
    void SetHeadMoves(BOOL enable);
    void EnableClock(bool enable);                                                                                       // 0xA1460
    void Sleep(int elapsed, int fpsLimit);                                                                               // 0xA0090
    CPed* CreatePed(int model, CVector position, float rotation, int a6, int a7);                                        // 0xA0110
    BOOL RemovePed(CPed* ped);                                                                                           // 0xA0210
    CVehicle* CreateVehicle(int model, CVector position, float rotation, BOOL hasSiren);                                 // 0xA0250
    CObject* CreateObject(int model, CVector position, CVector rotation, float drawDistance, char hourOn, char hourOff); // 0xA0330
    void ProcessInputEnabling();                                                                                         // 0xA05D0
    void ProcessFrameLimiter();                                                                                          // 0xA1C10

private:
    bool m_bInitialized = false;
    bool m_bGameStarted = false;
    bool m_bGameplayAssetsRequested = false;
    bool m_bGameplayReady = false;
};
