/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * The original code is copyright (c) 2026, 1therealcloud.
 */

#pragma once

#include <Windows.h>
#include <d3d9.h>

#include <cstddef>
#include <vector>

class CRender
{
public:
    struct Vector3
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };

    explicit CRender(IDirect3DDevice9* device);
    ~CRender();

    CRender(const CRender&) = delete;
    CRender& operator=(const CRender&) = delete;

    bool IsValid() const;

    HRESULT BeginRender();
    HRESULT EndRender();

    void OnLostDevice();
    void OnResetDevice();

    void DrawLine(float x1, float y1, float x2, float y2, float width, D3DCOLOR color);
    void DrawBox(float x, float y, float width, float height, D3DCOLOR color);
    void DrawBorder(float x, float y, float width, float height, float borderWidth, D3DCOLOR color);
    void DrawBorderedBox(float x, float y, float width, float height, float borderWidth, D3DCOLOR color, D3DCOLOR borderColor);
    void DrawPolygon(float x, float y, float width, float height, float rotation, unsigned int sides, D3DCOLOR color);
    void DrawCircle(float x, float y, float radius, unsigned int sides, D3DCOLOR color);

    bool WorldToScreen(const Vector3& world, float& x, float& y, float* depth = nullptr) const;
    bool ScreenRay(POINT point, Vector3& origin, Vector3& target) const;
    bool GetAxisDragOffset(const Vector3& world, const Vector3& axis, float pixels, bool vertical, float& offset) const;

    IDirect3DDevice9* GetDevice() const
    {
        return m_device;
    }

private:
    struct Vertex
    {
        float x;
        float y;
        float z;
        float rhw;
        D3DCOLOR color;
    };

    struct SavedState
    {
        IDirect3DBaseTexture9* texture0 = nullptr;
        IDirect3DVertexShader9* vertexShader = nullptr;
        IDirect3DPixelShader9* pixelShader = nullptr;
        IDirect3DVertexDeclaration9* declaration = nullptr;
        IDirect3DVertexBuffer9* stream = nullptr;
        UINT streamOffset = 0;
        UINT streamStride = 0;
        DWORD fvf = 0;

        DWORD alphaBlendEnable = 0;
        DWORD srcBlend = 0;
        DWORD destBlend = 0;
        DWORD separateAlphaBlendEnable = 0;
        DWORD srcBlendAlpha = 0;
        DWORD destBlendAlpha = 0;
        DWORD blendOp = 0;
        DWORD blendOpAlpha = 0;
        DWORD alphaTestEnable = 0;
        DWORD zEnable = 0;
        DWORD zWriteEnable = 0;
        DWORD cullMode = 0;
        DWORD lighting = 0;
        DWORD fogEnable = 0;
        DWORD scissorTestEnable = 0;
        DWORD multisampleAntialias = 0;
        DWORD antialiasedLineEnable = 0;
        DWORD colorWriteEnable = 0;

        DWORD colorOp = 0;
        DWORD colorArg1 = 0;
        DWORD colorArg2 = 0;
        DWORD alphaOp = 0;
        DWORD alphaArg1 = 0;
        DWORD alphaArg2 = 0;
        DWORD colorOp1 = 0;
        DWORD alphaOp1 = 0;
    };

private:
    static constexpr DWORD FVF = D3DFVF_XYZRHW | D3DFVF_DIFFUSE;

    bool EnsureVertexBuffer(std::size_t vertexCount);
    HRESULT Flush();

    void AddTriangle(const Vertex& a, const Vertex& b, const Vertex& c);
    void AddQuad(float left, float top, float right, float bottom, D3DCOLOR color);

    void SaveState();
    void ApplyState();
    void RestoreState();
    void ReleaseSavedState();
    void ReleaseVertexBuffer();

    static D3DMATRIX Multiply(const D3DMATRIX& a, const D3DMATRIX& b);

private:
    IDirect3DDevice9* m_device = nullptr;
    IDirect3DVertexBuffer9* m_vertexBuffer = nullptr;
    std::size_t m_vertexCapacity = 0;

    SavedState m_savedState{};
    std::vector<Vertex> m_vertices;
    bool m_rendering = false;
};
