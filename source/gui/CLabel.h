#pragma once

#include <Windows.h>
#include <d3d9.h>

#include <cstddef>

#include "utils/math/CVector.h"

class CLabel
{
public:
    IDirect3DDevice9* m_pDevice; // 0x00
    void* m_pSprite;             // 0x04, original R5: ID3DXSprite*

public:
    explicit CLabel(IDirect3DDevice9* device);
    ~CLabel();

    CLabel(const CLabel&) = delete;
    CLabel& operator=(const CLabel&) = delete;

    void OnLostDevice();
    void OnResetDevice();
    bool HasNoObstacles(CVector position);
    void Begin();
    void End();
    void Draw(CVector* position, const char* text, D3DCOLOR color, BOOL shadow, bool testLineOfSight);
};
