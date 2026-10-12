#pragma once
#include <cstring>

#include <Windows.h>
#include <d3d9.h>
#include <cstddef>
#include <cstdint>

#include "CEntity.h"

class CObject;
namespace AimStuff
{
    struct Aim;
}

class CPed : public CEntity
{
public:
    static constexpr std::size_t MAX_ACCESSORIES = 10;

    static void* GetLocalPedHandle();
    static bool GetLocalPlayerPosition(CVector& result);
    static void GetNativeBonePosition(void* ped, CVector* position, unsigned int bone, bool update);

    struct Accessory
    {
        int m_nModel = 0;
        int m_nBone = 0;
        CVector m_offset{};
        CVector m_rotation{};
        CVector m_scale{};
        D3DCOLOR m_firstMaterialColor = 0;
        D3DCOLOR m_secondMaterialColor = 0;
    };

    BOOL m_bUsingCellphone = FALSE; // 0x048

    struct Accessories
    {
        BOOL m_bNotEmpty[MAX_ACCESSORIES]{};   // 0x04C
        Accessory m_info[MAX_ACCESSORIES]{};   // 0x074
        CObject* m_pObject[MAX_ACCESSORIES]{}; // 0x27C
    } m_accessories;

    bool HasNativePed() const
    {
        return HasCurrentNativePed();
    }
    bool HasCurrentNativePed() const;
    bool IsValid() const
    {
        return HasCurrentNativePed() && CEntity::IsValid();
    }
    BOOL DoesExist() const
    {
        return HasCurrentNativePed() && CEntity::DoesExist();
    }
    void* GetNativeHandle() const
    {
        return HasCurrentNativePed() ? m_pGameEntity : nullptr;
    }
    void* GetNativePedHandle() const
    {
        return HasCurrentNativePed() ? m_pGamePed : nullptr;
    }
    bool IsHoldingStuff() const
    {
        return m_stuff.m_nType != 0;
    }
    void* m_pGamePed = nullptr;            // 0x2A4, GTA CPed*
    std::uint32_t pad_2a8[2]{};            // 0x2A8
    std::uint8_t m_nPlayerNumber = 0;      // 0x2B0
    std::uint32_t pad_2b1[2]{};            // 0x2B1
    std::uint32_t m_parachuteObject = 0;   // 0x2B9
    std::uint32_t m_urinatingParticle = 0; // 0x2BD

    struct Stuff
    {
        int m_nType = 0;
        std::uint32_t m_object = 0;
        int m_nDrunkLevel = 0;
    } m_stuff;

    std::uint32_t m_arrow = 0;          // 0x2CD
    std::uint8_t field_2d1 = 0;         // 0x2D1
    BOOL m_bDoesDancing = FALSE;        // 0x2D2
    std::uint32_t m_nDanceStyle = 0;    // 0x2D6
    std::uint32_t m_nLastDanceMove = 0; // 0x2DA
    std::uint8_t pad_2de[20]{};         // 0x2DE
    BOOL m_bDoesUrinating = FALSE;      // 0x2F2
    std::uint8_t pad_2f6[55]{};         // 0x2F6

    // R5 ProcessControl reads only these three bytes here. The original semantic
    // member names are unknown, so expose them by their verified input effects.
    bool SuppressesJumpInput() const
    {
        return pad_2f6[0] != 0;
    }
    bool SuppressesShockButtonLInput() const
    {
        return pad_2f6[1] != 0;
    }
    bool SuppressesCrossInput() const
    {
        return pad_2f6[2] != 0;
    }

    CPed();
    CPed(int playerNumber, int model, CVector position, float rotation);
    ~CPed() override;
    CPed(const CPed&) = delete;
    CPed& operator=(const CPed&) = delete;

    void ResetPointers();
    void SetInitialState();

    std::uint32_t GetVehicleRef() const;
    void DeleteArrow();
    BOOL IsOnScreen() const;

    float GetHealth() const;
    void SetHealth(float value);
    float GetArmour() const;
    float GetAimZ() const;
    void SetAim(const AimStuff::Aim* aim);
    void SetAimZ(float value);
    void SetArmour(float value);
    int GetFlags() const;
    void SetFlags(int value);
    BOOL IsDead() const;
    BOOL RestoreAfterDeath(const CVector& position, float rotation);
    char GetState() const;
    void SetState(char value);

    float GetRotation() const;
    void ForceRotation(float value);
    void SetRotation(float value);

    BOOL IsPassenger() const;
    BOOL IsInVehicle() const;
    void* GetVehicle() const;
    float GetDistanceToEntity(const CEntity* entity) const;
    int GetVehicleSeatIndex() const;
    bool HasVehicleEntryTask() const;
    bool HasVehicleExitTask() const;
    void PutIntoVehicle(std::uint32_t vehicle, int seat);
    void EnterVehicle(std::uint32_t vehicle, BOOL asPassenger);
    void ExitVehicle();
    bool StartPassengerDriveBy();
    bool IsDoingPassengerDriveBy() const;
    void StopPassengerDriveBy();
    bool HasAmmoForCurrentWeapon() const;
    void WarpFromVehicle(CVector putAt);

    void SetSpawnInfo(const CVector* position, float rotation);
    void SetControllable(BOOL enable);
    void ResetKeys();
    short GetKeys(short* leftStickX, short* leftStickY) const;
    void SetKeys(short buttons, short leftStickX, short leftStickY);
    void SetPlayerStat(int index, std::uint16_t value); // 0xAE450
    BOOL CopyAppearanceFrom(const CPed& source);
    void* GetFloor() const; // 0xACA10

    void HandsUp();
    BOOL DoesHandsUp() const;
    void HoldObject(int model);
    void Extinguish();

    void SetWalkStyle(const char* name);
    void SetFightingStyle(int style);
    int GetFightingStyle() const;

    int ApplyStuff();
    void GiveStuff(int type);
    int GetStuff();
    void DropStuff();

    void ProcessDrunk();
    int GetDrunkLevel();           // 0xADFA0
    void SetDrunkLevel(int level); // 0xADFB0

    std::uint8_t GetCurrentWeapon() const;
    std::uint8_t GetWeaponState() const;
    void SetWeaponState(std::uint8_t state);
    bool GetWeaponData(int slot, int& weapon, std::uint32_t& ammo) const;
    std::uint8_t GetWeaponSlot() const;
    void* GetTargetEntity() const;
    std::uint8_t GetDeathReason(void*& source) const;
    void ClearWeapons();
    void GiveWeapon(int weapon, int ammo);
    void SetWeaponAmmo(int weapon, int ammo);
    void SetArmedWeapon(int weapon);

    void PerformAnimation(const char* name, const char* file, float frameDelta, int loop, int lockX, int lockY, int lockF, int time); // 0xAD230
    void ClearTasks();
    void ProcessAnimation();
    void LinkToInterior(char id, BOOL refresh);

    BOOL IsOnGround() const;
    void ResetDamageEntity();
    void* GetContactEntity() const;
    void* GetContactVehicle() const;
    int GetStat() const;
    BOOL PerformingCustomAnimation() const;

    void StartDancing(int style);                     // 0xADAD0
    void StopDancing();                               // 0xADB20
    BOOL DoesDancing() const;                         // 0xADB60
    const char* GetAnimationForDance(int move) const; // 0xADB70
    void ProcessDancing();                            // 0xB00B0

    void LookAt(const CVector& target);
    void StopLooking(); // r5 rva not confirmed

    void ApplyCommandTask(const char* name, int a3, int a4, int a5, int a6, int a7, int a8, int a9, int a10, int a11);
    void DestroyCommandTask();

    void EnableCellphone(BOOL enable);
    BOOL UsingCellphone() const;

    void StartUrinating();
    void StopUrinating();
    BOOL DoesUrinating() const;

    void SetShopName(const char* name);
    const char* GetLoadedShoppingDataSubsection() const;
    void LoadShoppingDataSubsection(const char* name);

    void CreateArrow(int color);
    void SetModelIndex(int model); // 0xAFF50
    void Destroy();

    void SetCameraMode(char mode);
    void SetCameraExtZoomAndAspectRatio(float extZoom, float aspectRatio);
    char GetCameraMode() const;

    BOOL HasAccessory() const;
    void DeleteAccessory(int slot);
    BOOL GetAccessoryState(int slot) const;
    void DeleteAllAccessories();
    void AddAccessory(int slot, const Accessory* info); // 0xB0B10
    CObject* GetAccessory(int slot) const;
    void ProcessAccessories();
    bool GetAccessoryMatrix(int slot, CMatrix* matrix) const;
    bool UpdateAccessory(int slot, const Accessory& info);
    static bool BuildAccessoryMatrix(const CMatrix& bone, const Accessory& info, CMatrix* matrix)
    {
        const auto finite = [](const CVector& v) {
            return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
        };
        if (!matrix || !finite(info.m_offset) || !finite(info.m_rotation) || !finite(info.m_scale) || info.m_scale.x < 0 || info.m_scale.y < 0 || info.m_scale.z < 0 ||
            !finite(bone.GetPosition()) || !finite(bone.GetRight()) || !finite(bone.GetForward()) || !finite(bone.GetUp()))
            return false;
        CMatrix result = bone;
        result.GetPosition() = bone.TransformPoint(info.m_offset);
        constexpr float radians = 0.017453292519943295f;
        CMatrix rotation = CMatrix::Unity();
        rotation.SetRotateX(info.m_rotation.x * radians);
        result *= rotation;
        rotation.SetRotateY(info.m_rotation.y * radians);
        result *= rotation;
        rotation.SetRotateZ(info.m_rotation.z * radians);
        result *= rotation;
        result.GetRight() *= info.m_scale.x;
        result.GetForward() *= info.m_scale.y;
        result.GetUp() *= info.m_scale.z;
        if (!finite(result.GetPosition()) || !finite(result.GetRight()) || !finite(result.GetForward()) || !finite(result.GetUp()))
            return false;
        *matrix = result;
        return true;
    }
    void GetBonePosition(unsigned int boneId, CVector* outPosition) const;

private:
    struct PendingAnimation
    {
        char name[256]{};
        char library[256]{};
        void* ped{};
        std::uint32_t handle{};
        DWORD requested{};
        float delta{};
        int loop{}, lockX{}, lockY{}, freeze{}, time{};
        bool active{};
    } m_pendingAnimation;
};
