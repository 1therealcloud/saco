/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * The original code is copyright (c) 2026, 1therealcloud.
 */

#include <stdafx.h>

#include "CObjectMaterialText.h"
#include "render/CFonts.h"
#include "textures.h"

#include "game/RenderWare/RenderWare.h"
#include <condition_variable>
#include <thread>

namespace
{
    struct MaterialFont
    {
        std::string face;
        int size;
        bool bold;
        std::unique_ptr<CFont> font;
    };

    class MaterialFontCache
    {
    public:
        MaterialFontCache()
            : m_worker([this] {
                  Run();
              })
        {}
        ~MaterialFontCache()
        {
            Clear();
            {
                std::lock_guard lock(m_mutex);
                m_stopping = true;
            }
            m_ready.notify_one();
            m_worker.join();
        }

        void Retire(std::unique_ptr<CFont> font)
        {
            font->ReleaseDeviceObjects();
            {
                std::lock_guard lock(m_mutex);
                m_retired.push_back(std::move(font));
            }
            m_ready.notify_one();
        }

        void Clear()
        {
            for (auto& entry : fonts)
                Retire(std::move(entry.font));
            fonts.clear();
        }

        std::vector<MaterialFont> fonts;

    private:
        void Run()
        {
            for (;;)
            {
                std::vector<std::unique_ptr<CFont>> retired;
                {
                    std::unique_lock lock(m_mutex);
                    m_ready.wait(lock, [this] {
                        return m_stopping || !m_retired.empty();
                    });
                    retired.swap(m_retired);
                    if (retired.empty() && m_stopping)
                        return;
                }
                ZoneScopedN("Material font cleanup");
                retired.clear();
            }
        }

        std::mutex m_mutex;
        std::condition_variable m_ready;
        std::vector<std::unique_ptr<CFont>> m_retired;
        bool m_stopping = false;
        std::thread m_worker;
    };
    std::unordered_map<const CObjectMaterialText*, std::unique_ptr<MaterialFontCache>> g_materialFonts;

    struct MaterialTexture
    {
        std::string key;
        game::render::textures::Texture texture;
        std::size_t bytes;
    };
    std::unordered_map<const CObjectMaterialText*, std::vector<MaterialTexture>> g_materialTextures;

    template <typename T>
    void SafeRelease(T*& object)
    {
        if (!object)
            return;

        object->Release();
        object = nullptr;
    }

    struct TextureResources
    {
        IDirect3DTexture9* texture = nullptr;
        IDirect3DSurface9* surface = nullptr;
        IDirect3DSurface9* depthStencil = nullptr;
        ~TextureResources()
        {
            SafeRelease(depthStencil);
            SafeRelease(surface);
            SafeRelease(texture);
        }
    };

    DWORD GetTextFormat(std::uint8_t align)
    {
        ZoneScoped;
        // R5 +0x70984:
        //   0 -> 0
        //   1 -> DT_CENTER | DT_VCENTER (5)
        //   2 -> DT_RIGHT (2)
        // then DT_NOCLIP is ORed immediately before DrawTextA.
        switch (align)
        {
            case 1:
                return DT_CENTER | DT_VCENTER;
            case 2:
                return DT_RIGHT;
            default:
                return 0;
        }
    }

    class RenderTargetState
    {
    public:
        explicit RenderTargetState(IDirect3DDevice9* device) : m_device(device)
        {
            ZoneScoped;
        }

        RenderTargetState(const RenderTargetState&) = delete;
        RenderTargetState& operator=(const RenderTargetState&) = delete;

        ~RenderTargetState()
        {
            ZoneScoped;
            Restore();
        }

        bool Begin(IDirect3DSurface9* renderTarget, IDirect3DSurface9* depthStencil, UINT width, UINT height)
        {
            ZoneScoped;
            if (!m_device || m_active)
                return false;
            if (FAILED(m_device->GetRenderTarget(0, &m_oldRenderTarget)) || FAILED(m_device->GetViewport(&m_oldViewport)))
            {
                SafeRelease(m_oldRenderTarget);
                return false;
            }
            const HRESULT depthResult = m_device->GetDepthStencilSurface(&m_oldDepthStencil);
            if (FAILED(depthResult) && depthResult != D3DERR_NOTFOUND)
            {
                SafeRelease(m_oldRenderTarget);
                return false;
            }
            m_active = true;
            D3DVIEWPORT9 viewport{0, 0, width, height, 0.0f, 1.0f};
            if (FAILED(m_device->SetDepthStencilSurface(nullptr)) || FAILED(m_device->SetRenderTarget(0, renderTarget)) || FAILED(m_device->SetDepthStencilSurface(depthStencil)) ||
                FAILED(m_device->SetViewport(&viewport)))
                return false;
            m_sceneBegun = SUCCEEDED(m_device->BeginScene());
            return m_sceneBegun;
        }

        void Restore()
        {
            ZoneScoped;
            if (!m_device || !m_active)
                return;

            if (m_sceneBegun)
            {
                m_device->EndScene();
                m_sceneBegun = false;
            }

            m_device->SetRenderTarget(0, m_oldRenderTarget);
            m_device->SetDepthStencilSurface(m_oldDepthStencil);
            m_device->SetViewport(&m_oldViewport);

            SafeRelease(m_oldRenderTarget);
            SafeRelease(m_oldDepthStencil);
            m_active = false;
        }

    private:
        IDirect3DDevice9* m_device = nullptr;
        IDirect3DSurface9* m_oldRenderTarget = nullptr;
        IDirect3DSurface9* m_oldDepthStencil = nullptr;
        D3DVIEWPORT9 m_oldViewport{};
        bool m_sceneBegun = false;
        bool m_active = false;
    };
}

CObjectMaterialText::CObjectMaterialText(IDirect3DDevice9* device) : m_pDevice(device), m_pTextSprite(nullptr), m_pSprite(nullptr)
{
    // R5 calls +0x70860 here. In the original that creates two ID3DXSprite
    // objects. They are backend-only state, so SACO keeps the exact layout but
    // has no D3DX resources to create.
    RestoreDeviceObjects();
}

CObjectMaterialText::~CObjectMaterialText()
{
    // +0x708A0 is only a jump to +0x70830.
    DeleteDeviceObjects();
    g_materialFonts.erase(this);
}

void CObjectMaterialText::ClearCache()
{
    g_materialTextures.erase(this);
    if (const auto it = g_materialFonts.find(this); it != g_materialFonts.end())
        it->second->Clear();
}

void CObjectMaterialText::DeleteDeviceObjects()
{
    ClearCache();
    // Original +0x70830 releases +0x08 first, then +0x04. The D3DX-free
    // implementation never creates either object, but preserve the state/order.
    m_pSprite = nullptr;
    m_pTextSprite = nullptr;
}

void CObjectMaterialText::RestoreDeviceObjects()
{
    ZoneScoped;
    // Original +0x70860:
    //   D3DXCreateSprite(m_pDevice, &m_pTextSprite);
    //   D3DXCreateSprite(m_pDevice, &m_pSprite);
    // Neither sprite is otherwise used by SACO's backend. +0x708B0 uses the
    // first sprite only as the batching object passed to ID3DXFont::DrawTextA.
    m_pTextSprite = nullptr;
    m_pSprite = nullptr;
}

IDirect3DTexture9* CObjectMaterialText::Create(const char* text, const char* font, int fontSize, int width, int height, D3DCOLOR fontColor, D3DCOLOR backgroundColor, bool bold,
                                               std::uint8_t align)
{
    ZoneScoped;
    // R5 +0x708B0 has two early-outs: no D3D device, or no global GUI/font
    // subsystem. CFont is the SACO replacement for the latter, so only the
    // device can be checked before constructing it.
    if (!m_pDevice || !text || !font || width <= 0 || height <= 0)
        return nullptr;

    // Present in R5 even though the value is not consumed afterwards.
    D3DDISPLAYMODE displayMode{};
    m_pDevice->GetDisplayMode(0, &displayMode);

    TextureResources resources;
    auto*& texture = resources.texture;

    // Exact R5 texture format selection:
    // transparent background -> A8R8G8B8, otherwise X8R8G8B8.
    const D3DFORMAT textureFormat = backgroundColor == 0 ? D3DFMT_A8R8G8B8 : D3DFMT_X8R8G8B8;

    if (FAILED(m_pDevice->CreateTexture(static_cast<UINT>(width), static_cast<UINT>(height), 1, D3DUSAGE_RENDERTARGET, textureFormat, D3DPOOL_DEFAULT, &texture, nullptr)))
    {
        return nullptr;
    }

    auto*& surface = resources.surface;
    if (FAILED(texture->GetSurfaceLevel(0, &surface)))
        return nullptr;

    D3DSURFACE_DESC surfaceDesc{};
    if (surface)
        surface->GetDesc(&surfaceDesc);

    // D3DXCreateRenderToSurface in R5 is created with DepthStencil=TRUE and
    // D3DFMT_D16. A dedicated depth surface gives the same target setup.
    auto*& depthStencil = resources.depthStencil;
    const HRESULT depthResult =
        m_pDevice->CreateDepthStencilSurface(static_cast<UINT>(width), static_cast<UINT>(height), D3DFMT_D16, D3DMULTISAMPLE_NONE, 0, TRUE, &depthStencil, nullptr);

    if (FAILED(depthResult))
    {
        return nullptr;
    }

    const int weight = bold ? FW_BOLD : FW_NORMAL;

    // R5's D3DXCreateFontA call uses MipLevels=1, DEFAULT_CHARSET and
    // Quality=DEFAULT_QUALITY. The accompanying CFonts patch adds the final
    // quality argument while preserving every existing caller's default.
    auto& cache = g_materialFonts[this];
    if (!cache)
    {
        try
        {
            cache = std::make_unique<MaterialFontCache>();
        }
        catch (const std::system_error&)
        {
#if defined(_DEBUG)
            Log("[material] failed to start font cleanup worker");
#endif // _DEBUG
            g_materialFonts.erase(this);
            return nullptr;
        }
    }
    auto& fonts = cache->fonts;
    auto found = std::find_if(fonts.begin(), fonts.end(), [font, fontSize, bold](const MaterialFont& entry) {
        return entry.face == font && entry.size == fontSize && entry.bold == bold;
    });
    if (found == fonts.end())
    {
        ZoneScopedN("Material font cache miss");
        ZoneText(font, std::strlen(font));
        ZoneValue(fontSize);
        auto created = std::make_unique<CFont>(m_pDevice, fontSize, 0, weight, font, text::CodePage, DEFAULT_CHARSET, DEFAULT_QUALITY, text);
        if (!created->IsValid())
        {
#if defined(_DEBUG)
            Log("SignText: Can't create font %s", font);
#endif // _DEBUG
            return nullptr;
        }
        if (fonts.size() >= 32)
        {
            cache->Retire(std::move(fonts.front().font));
            fonts.erase(fonts.begin());
        }
        fonts.push_back({font, fontSize, bold, std::move(created)});
        found = std::prev(fonts.end());
    }
    if (std::next(found) != fonts.end())
        std::rotate(found, std::next(found), fonts.end());
    CFont& textFont = *fonts.back().font;

    if (!textFont.IsValid())
    {
        // Exact R5 diagnostic string from samp.dll +0xEA704.
#if defined(_DEBUG)
        Log("SignText: Can't create font %s", font);
#endif // _DEBUG

        return nullptr;
    }

    RenderTargetState renderTarget(m_pDevice);
    if (!renderTarget.Begin(surface, depthStencil, static_cast<UINT>(width), static_cast<UINT>(height)))
        return nullptr;

    // R5 clears both target and Z-buffer with Z=0.0f and stencil=0.
    if (FAILED(m_pDevice->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, backgroundColor, 0.0f, 0)))
        return nullptr;

    RECT rect{};
    rect.right = width;
    rect.bottom = height;

    DWORD format = GetTextFormat(align);
    format |= DT_NOCLIP;

    // D3DXSprite::Begin(D3DXSPRITE_ALPHABLEND) normally installs
    // D3DBLEND_INVSRCALPHA. R5 then overrides only the transparent-background
    // path to D3DBLEND_BLENDFACTOR. The CFonts patch exposes that one state so
    // the D3DX-free renderer keeps the same blend choice.
    const D3DBLEND destBlend = backgroundColor == 0 ? D3DBLEND_BLENDFACTOR : D3DBLEND_INVSRCALPHA;
    textFont.DrawTextA(text, &rect, format, fontColor, destBlend);

    // ID3DXRenderToSurface::EndScene(0) restores the previous target/viewport.
    renderTarget.Restore();

    SafeRelease(surface);
    SafeRelease(depthStencil);

    return std::exchange(texture, nullptr);
}

RwTexture* CObjectMaterialText::CreateRwTexture(const char* text, const char* font, int fontSize, int width, int height, D3DCOLOR fontColor, D3DCOLOR backgroundColor, bool bold,
                                                std::uint8_t align, bool* cacheHit)
{
    ZoneScoped;
    if (cacheHit)
        *cacheHit = false;
    if (!text || !font || width <= 0 || height <= 0)
        return nullptr;
    std::string key(text);
    key.push_back('\0');
    key.append(font);
    key.push_back('\0');
    const std::array<std::uint32_t, 7> parameters{
        static_cast<std::uint32_t>(fontSize), static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height), fontColor, backgroundColor, bold ? 1u : 0u, align};
    key.append(reinterpret_cast<const char*>(parameters.data()), sizeof(parameters));
    auto& cache = g_materialTextures[this];
    auto found = std::find_if(cache.begin(), cache.end(), [&key](const MaterialTexture& entry) {
        return entry.key == key;
    });
    if (found != cache.end())
    {
        ZoneScopedN("Material texture cache hit");
        if (cacheHit)
            *cacheHit = true;
        if (std::next(found) != cache.end())
            std::rotate(found, std::next(found), cache.end());
        return RwTextureAddRef(cache.back().texture.get());
    }
    TextureResources resources;
    resources.texture = Create(text, font, fontSize, width, height, fontColor, backgroundColor, bold, align);
    if (!resources.texture || FAILED(resources.texture->GetSurfaceLevel(0, &resources.surface)))
        return nullptr;
    D3DSURFACE_DESC description{};
    if (FAILED(resources.surface->GetDesc(&description)))
        return nullptr;
    IDirect3DSurface9* pixels = nullptr;
    if (FAILED(m_pDevice->CreateOffscreenPlainSurface(description.Width, description.Height, description.Format, D3DPOOL_SYSTEMMEM, &pixels, nullptr)))
        return nullptr;
    HRESULT copied;
    {
        ZoneScopedN("Material GPU readback");
        copied = m_pDevice->GetRenderTargetData(resources.surface, pixels);
    }
    if (FAILED(copied))
    {
        SafeRelease(pixels);
        return nullptr;
    }
    auto* raster = RwRasterCreate(width, height, 32, rwRASTERTYPETEXTURE | rwRASTERFORMAT8888);
    if (!raster)
    {
        SafeRelease(pixels);
        return nullptr;
    }
    auto* destination = RwRasterLock(raster, 0, rwRASTERLOCKWRITE);
    D3DLOCKED_RECT source{};
    if (!destination || FAILED(pixels->LockRect(&source, nullptr, D3DLOCK_READONLY)))
    {
        if (destination)
            RwRasterUnlock(raster);
        RwRasterDestroy(raster);
        SafeRelease(pixels);
        return nullptr;
    }
    for (int row = 0; row < height; ++row)
    {
        auto* output = destination + row * raster->stride;
        std::memcpy(output, static_cast<const std::uint8_t*>(source.pBits) + row * source.Pitch, width * 4);
        if (description.Format == D3DFMT_X8R8G8B8)
            for (int column = 0; column < width; ++column)
                output[column * 4 + 3] = 0xFF;
    }
    pixels->UnlockRect();
    SafeRelease(pixels);
    RwRasterUnlock(raster);
    auto* texture = RwTextureCreate(raster);
    if (!texture)
        RwRasterDestroy(raster);
    else
    {
        RwTextureSetFilterMode(texture, rwFILTERLINEAR);
        constexpr std::size_t budget = 32 * 1024 * 1024;
        const auto bytes = static_cast<std::uint64_t>(width) * height * 4;
        if (bytes <= budget)
        {
            std::size_t used = 0;
            for (const auto& entry : cache)
                used += entry.bytes;
            while (!cache.empty() && (cache.size() >= 128 || used + bytes > budget))
            {
                used -= cache.front().bytes;
                cache.erase(cache.begin());
            }
            cache.push_back({std::move(key), game::render::textures::Texture(RwTextureAddRef(texture)), static_cast<std::size_t>(bytes)});
        }
    }
    return texture;
}
