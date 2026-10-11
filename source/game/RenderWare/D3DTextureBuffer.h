#include <cstdint>
/*
    Plugin-SDK file
    Authors: GTA Community. See more here
    https://github.com/DK22Pac/plugin-sdk
    Do not delete this comment block. Respect others' work!
*/
#pragma once

#ifdef _DX9_SDK_INSTALLED
#   include "d3d9.h"
#endif

class D3DTextureBuffer
{
public:
    std::uint32_t m_nFormat;
    std::uint32_t m_nWidth;
    std::int32_t m_nLevels; // -1 - not defined, 0 - full-chain, 1 - one level
    std::uint32_t m_nCapcacity;
    std::uint32_t m_nNumTexturesInBuffer;
    std::uint32_t m_nSize;
#ifdef _D3D9_H_
    IDirect3DTexture9** m_apTextures;
#else
    void** m_apTextures;
#endif

    void Clear();
    void Destroy();
    std::uint32_t GetTotalDataSize();
    void Resize(std::uint32_t newCapacity);
    void Setup(std::uint32_t format, std::int32_t width, std::int32_t bOneLevel, std::uint32_t capacity);
#ifdef _D3D9_H_
    IDirect3DTexture9* Pop();
    IDirect3DTexture9* Pop(std::uint32_t format, std::int32_t width, std::int32_t height, std::int32_t bOneLevel);
    bool Push(IDirect3DTexture9* texture);
    bool PushWithoutIncreasingCounter(IDirect3DTexture9* texture);
#else
    void* Pop();
    void* Pop(std::uint32_t format, std::int32_t width, std::int32_t height, std::int32_t bOneLevel);
    bool Push(void* texture);
    bool PushWithoutIncreasingCounter(void* texture);
#endif
};

static_assert(sizeof(D3DTextureBuffer) == 0x1C);
