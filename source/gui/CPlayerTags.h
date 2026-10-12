#pragma once

#include <Windows.h>
#include <d3d9.h>

#include <cstddef>

#include "utils/math/CVector.h"

class CPlayerTags
{
public:
    IDirect3DDevice9* m_pDevice;     // 0x00
    IDirect3DStateBlock9* m_pStates; // 0x04
    void* m_pSprite;                 // 0x08, original R5: ID3DXSprite*

public:
    explicit CPlayerTags(IDirect3DDevice9* device);
    ~CPlayerTags();

    CPlayerTags(const CPlayerTags&) = delete;
    CPlayerTags& operator=(const CPlayerTags&) = delete;

    void DrawTag(CVector* position, const char* text, D3DCOLOR color, float health, float armour, float distanceToCamera, bool drawStatus, int status);
    void EndHealthBar();
    void BeginLabel();
    void EndLabel();
    void DrawLabel(CVector* position, const char* text, D3DCOLOR color, float distanceToCamera, bool drawStatus, int status);
    void DrawHealthBar(CVector* position, float health, float armour, float distanceToCamera);
    void OnLostDevice();
    void OnResetDevice();
    void BeginHealthBar();

private:
    void DrawLabelAt(LONG x, LONG y, const char* text, D3DCOLOR color, bool drawStatus, int status);
    void DrawHealthBarAt(LONG x, LONG y, float health, float armour, bool fontBars = false);
};
