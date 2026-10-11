/*
    Plugin-SDK file
    Authors: GTA Community. See more here
    https://github.com/DK22Pac/plugin-sdk
    Do not delete this comment block. Respect others' work!
*/

#include <stdafx.h>
#include "game/RenderWare/RenderWare.h"
#include "D3DResourceSystem.h"

// 0x730900
void D3DResourceSystem::CancelBuffering()
{
    game::render::native::call<0x730900>();
}

// 0x7307F0
std::uint32_t D3DResourceSystem::GetTotalIndexDataSize()
{
    return game::render::native::call_return<std::uint32_t, 0x7307F0>();
}

// 0x730660
std::uint32_t D3DResourceSystem::GetTotalPixelsSize()
{
    return game::render::native::call_return<std::uint32_t, 0x730660>();
}

// 0x730830
void D3DResourceSystem::Init()
{
    game::render::native::call<0x730830>();
}

// 0x730AC0
void D3DResourceSystem::SetUseD3DResourceBuffering(bool bUse)
{
    game::render::native::call<0x730AC0, bool>(bUse);
}

// 0x730A00
void D3DResourceSystem::Shutdown()
{
    game::render::native::call<0x730A00>();
}

// 0x730740
void D3DResourceSystem::TidyUpD3DIndexBuffers(std::uint32_t count)
{
    game::render::native::call<0x730740, std::uint32_t>(count);
}

// 0x7305E0
void D3DResourceSystem::TidyUpD3DTextures(std::uint32_t count)
{
    game::render::native::call<0x7305E0, std::uint32_t>(count);
}

// 0x7306A0
std::int32_t D3DResourceSystem::CreateIndexBuffer(std::uint32_t numIndices, std::uint32_t format, void** ppIndexBuffer)
{
    return game::render::native::call_return<std::int32_t, 0x7306A0, std::uint32_t, std::uint32_t, void**>(numIndices, format, ppIndexBuffer);
}

// 0x730510
std::int32_t D3DResourceSystem::CreateTexture(std::int32_t width, std::int32_t height, std::uint32_t format, void** ppTexture)
{
    return game::render::native::call_return<std::int32_t, 0x730510, std::int32_t, std::int32_t, std::uint32_t, void**>(width, height, format, ppTexture);
}

// 0x730D30
void D3DResourceSystem::DestroyIndexBuffer(void* pIndexBuffer)
{
    game::render::native::call<0x730D30, void*>(pIndexBuffer);
}

// 0x730B70
void D3DResourceSystem::DestroyTexture(void* texture)
{
    game::render::native::call<0x730B70, void*>(texture);
}
