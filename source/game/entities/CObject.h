#pragma once

#include <d3d9.h>

#include <cstddef>
#include <cstdint>
#include "CEntity.h"

class CObject;
class CPed;

class CObject : public CEntity
{
public:
    bool m_bIsActive = true;
    bool m_bReadyToRender = false;
    std::uint32_t m_nNoDrawFrames = 0;
    int m_nModel = 0;
    bool m_bUsesModelDrawDistance = false;
    bool m_bDontCollideWithCamera = false;
    float m_fDrawDistance = 0.0f;
    float m_fBoundingRadius = 0.0f;
    CVector m_position{};
    float m_fDistanceToCamera = 0.0f;
    bool m_bDrawLast = false;
    char pad_2[64]{};
    CVector m_rotation{};
    char pad_3[5]{};
    std::uint16_t m_nAttachedToVehicle = 0xFFFF;
    std::uint16_t m_nAttachedToObject = 0xFFFF;
    CVector m_attachOffset{};
    CVector m_attachRotation{};
    bool m_bSyncRotation = true;
    // samp stores a 64-byte matrix here; our CMatrix also owns attachment metadata
    RwMatrix m_targetMatrix{};
    char pad_4[148]{};
    char m_bMoving = 0;
    float m_fSpeed = 0.0f;
    char pad_5[99]{};

    enum
    {
        MAX_MATERIALS = 16
    };
    enum MaterialType
    {
        MATERIAL_TYPE_NONE,
        MATERIAL_TYPE_TEXTURE,
        MATERIAL_TYPE_TEXT
    };
    enum MaterialSize
    {
        MATERIAL_SIZE_32X32 = 10,
        MATERIAL_SIZE_64X32 = 20,
        MATERIAL_SIZE_64X64 = 30,
        MATERIAL_SIZE_128X32 = 40,
        MATERIAL_SIZE_128X64 = 50,
        MATERIAL_SIZE_128X128 = 60,
        MATERIAL_SIZE_256X32 = 70,
        MATERIAL_SIZE_256X64 = 80,
        MATERIAL_SIZE_256X128 = 90,
        MATERIAL_SIZE_256X256 = 100,
        MATERIAL_SIZE_512X64 = 110,
        MATERIAL_SIZE_512X128 = 120,
        MATERIAL_SIZE_512X256 = 130,
        MATERIAL_SIZE_512X512 = 140
    };
    struct ObjectMaterial
    {
        RwTexture* m_pTextBackground[MAX_MATERIALS]{};
        D3DCOLOR m_color[MAX_MATERIALS]{};
        char pad_6[68]{};
        int m_nType[MAX_MATERIALS]{};
        BOOL m_bTextureWasCreated[MAX_MATERIALS]{};
        struct MaterialText
        {
            char m_nMaterialIndex = 0;
            char pad_0[137]{};
            char m_nMaterialSize = 0;
            char m_szFont[65]{};
            char m_nFontSize = 0;
            bool m_bBold = false;
            D3DCOLOR m_fontColor = 0;
            D3DCOLOR m_backgroundColor = 0;
            char m_align = 0;
        };
        MaterialText m_textInfo[MAX_MATERIALS]{};
        char* m_szText[MAX_MATERIALS]{};
        IDirect3DTexture9* m_pBackgroundTexture[MAX_MATERIALS]{};
        IDirect3DTexture9* m_pTexture[MAX_MATERIALS]{};
    };
    ObjectMaterial m_material{};
    BOOL m_bHasCustomMaterial = FALSE;
    char pad_9[13]{};

    CObject(int model, CVector position, CVector rotation, float drawDistance = 0.0f, int customLighting = 0, char hourOn = 0, char hourOff = 0); // 0xA9110
    ~CObject() override;                                                                                                                          // R5 RVA unverified
    void Add() override;
    void Remove() override;
    float GetDistance(const CMatrix* matrix) const; // 0xA7FC0
    void Stop();                                    // 0xA8030
    void SetRotation(const CVector* rotation);      // 0xA80A0
    CVector GetRotation() const;
    void SetAttachedToVehicle(std::uint16_t id, const CVector* offset, const CVector* rotation);                   // 0xA8110
    void SetAttachedToObject(std::uint16_t id, const CVector* offset, const CVector* rotation, char syncRotation); // 0xA81A0
    void AttachToVehicle(CEntity* vehicle);                                                                        // 0xA8240
    void AttachToObject(CObject* object);                                                                          // 0xA82C0
    void AttachToPlayer(CPed* ped, const CVector* offset, const CVector* rotation);
    void SetCameraCollision(bool enable);
    void Rotate(CVector rotation);                                                                                                                                      // 0xA83C0
    BOOL AttachedToMovingEntity() const;                                                                                                                                // 0xA84C0
    void SetMaterial(int model, int index, const char* txd, const char* texture, D3DCOLOR color);                                                                       // 0xA8530
    void SetMaterialText(int index, const char* text, char size, const char* font, char fontSize, bool bold, D3DCOLOR fontColor, D3DCOLOR backgroundColor, char align); // 0xA86B0
    static bool GetMaterialSize(int size, int* width, int* height);                                                                                                     // 0xA8C80
    void Render();                                                                                                                                                      // 0xA8F60
    void Process(float elapsedSeconds);                                                                                                                                 // 0xA9650
    void ConstructMaterialText();                                                                                                                                       // 0xA9EE0
    void ShutdownMaterialText();                                                                                                                                        // 0xA8ED0
    void Draw();                                                                                                                                                        // 0xA9F90
    void SetPoolManaged(bool managed);
    float MoveTo(CVector position, float speed, CVector rotation = {-1000.0f, -1000.0f, -1000.0f}); // 0xA9BD0 (R5 movement setup; saco return value)
    void Detach();
    static void ProcessAll(float elapsedSeconds);
    static void ConstructAllMaterialText();

private:
    void ClearMaterial(int index);
};

namespace game::objects::detail
{
    bool IgnoresCamera(void* entity);
    void RenderNativeObject(void* entity, void(__cdecl* draw)(void*));
}
