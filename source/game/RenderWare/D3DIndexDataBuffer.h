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

class D3DIndexDataBuffer
{
public:
    std::uint32_t m_nFormat;
    std::int32_t field_4;
    std::uint32_t m_nCapcacity;
    std::uint32_t m_nNumDatasInBuffer;
    std::uint32_t m_nSize;
#ifdef _D3D9_H_
    IDirect3DIndexBuffer9** m_apIndexData;
#else
    void** m_apIndexData;
#endif

    void Clear();
    void Destroy();
    std::uint32_t GetTotalDataSize();
    void Resize(std::uint32_t newCapacity);
    void Setup(std::uint32_t format, std::int32_t _f4, std::uint32_t capacity);
#ifdef _D3D9_H_
    IDirect3DIndexBuffer9* Pop();
    IDirect3DIndexBuffer9* Pop(std::uint32_t indexCount);
    bool Push(IDirect3DIndexBuffer9* indexBuffer);
    bool PushWithoutIncreasingCounter(IDirect3DIndexBuffer9* indexBuffer);
#else
    void* Pop();
    void* Pop(std::uint32_t indexCount);
    bool Push(void* indexBuffer);
    bool PushWithoutIncreasingCounter(void* indexBuffer);
#endif
};

static_assert(sizeof(D3DIndexDataBuffer) == 0x18);
