#include <cstdint>
/*
    Plugin-SDK file
    Authors: GTA Community. See more here
    https://github.com/DK22Pac/plugin-sdk
    Do not delete this comment block. Respect others' work!
*/
#pragma once

#include "D3DTextureBuffer.h"
#include "D3DIndexDataBuffer.h"
#ifdef _DX9_SDK_INSTALLED
#   include "d3d9.h"
#endif

class D3DResourceSystem
{
public:
    static bool& UseD3DResourceBuffering;
    static std::uint32_t& FreeTextureBufferIndex;
    static D3DTextureBuffer& TextureBuffer;
    static D3DIndexDataBuffer& IndexDataBuffer;

    static void CancelBuffering();
    static std::uint32_t GetTotalIndexDataSize();
    static std::uint32_t GetTotalPixelsSize();
    static void Init();
    static void SetUseD3DResourceBuffering(bool bUse);
    static void Shutdown();
    static void TidyUpD3DIndexBuffers(std::uint32_t count);
    static void TidyUpD3DTextures(std::uint32_t count);
    static std::int32_t CreateIndexBuffer(std::uint32_t numIndices, std::uint32_t format, void** ppIndexBuffer);
    static std::int32_t CreateTexture(std::int32_t width, std::int32_t height, std::uint32_t format, void** ppTexture);
    static void DestroyIndexBuffer(void* pIndexBuffer);
    static void DestroyTexture(void* texture);
};
