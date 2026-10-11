#include <stdafx.h>

#include "CLicensePlate.h"

#include "main.h"
#include "render/CFonts.h"
#include "game/RenderWare/RenderWare.h"

#include <memory>

namespace
{
    template <typename T>
    void SafeRelease(T*& object)
    {
        if (!object)
            return;

        object->Release();
        object = nullptr;
    }

    // Native D3D9 replacement for the ID3DXRenderToSurface stored by R5 at
    // CLicensePlate +0x04. Keeping this object behind a pointer preserves the
    // original 0x24-byte CLicensePlate layout.
    class RenderToSurface
    {
    public:
        RenderToSurface(IDirect3DDevice9* device, UINT width, UINT height) : m_device(device), m_width(width), m_height(height)
        {
            if (!m_device)
                return;

            // R5: D3DXCreateRenderToSurface(..., TRUE, D3DFMT_D16, ...).
            if (FAILED(m_device->CreateDepthStencilSurface(width, height, D3DFMT_D16, D3DMULTISAMPLE_NONE, 0, TRUE, &m_depthStencil, nullptr)))
            {
                m_depthStencil = nullptr;
            }
        }

        ~RenderToSurface()
        {
            if (m_active)
                EndScene();

            SafeRelease(m_depthStencil);
        }

        RenderToSurface(const RenderToSurface&) = delete;
        RenderToSurface& operator=(const RenderToSurface&) = delete;

        bool IsValid() const
        {
            return m_device && m_depthStencil;
        }

        HRESULT BeginScene(IDirect3DSurface9* target)
        {
            if (!IsValid() || !target || m_active)
                return D3DERR_INVALIDCALL;

            if (FAILED(m_device->GetRenderTarget(0, &m_oldTarget)) || FAILED(m_device->GetViewport(&m_oldViewport)))
            {
                SafeRelease(m_oldTarget);
                return D3DERR_INVALIDCALL;
            }
            const HRESULT depthResult = m_device->GetDepthStencilSurface(&m_oldDepthStencil);
            if (FAILED(depthResult) && depthResult != D3DERR_NOTFOUND)
            {
                SafeRelease(m_oldTarget);
                return depthResult;
            }

            HRESULT result = m_device->SetDepthStencilSurface(nullptr);
            if (SUCCEEDED(result))
                result = m_device->SetRenderTarget(0, target);
            if (SUCCEEDED(result))
                result = m_device->SetDepthStencilSurface(m_depthStencil);

            if (SUCCEEDED(result))
            {
                D3DVIEWPORT9 viewport{};
                viewport.X = 0;
                viewport.Y = 0;
                viewport.Width = m_width;
                viewport.Height = m_height;
                viewport.MinZ = 0.0f;
                viewport.MaxZ = 1.0f;
                result = m_device->SetViewport(&viewport);
            }

            if (SUCCEEDED(result))
            {
                const HRESULT beginResult = m_device->BeginScene();
                m_sceneBegun = SUCCEEDED(beginResult);
                result = beginResult;
            }

            // ID3DXRenderToSurface owns the target switch until EndScene even
            // when the caller ignores the BeginScene HRESULT. R5 does ignore it.
            m_active = true;
            return result;
        }

        HRESULT EndScene()
        {
            if (!m_device || !m_active)
                return D3DERR_INVALIDCALL;

            HRESULT result = D3D_OK;

            if (m_sceneBegun)
            {
                result = m_device->EndScene();
                m_sceneBegun = false;
            }

            m_device->SetRenderTarget(0, m_oldTarget);
            m_device->SetDepthStencilSurface(m_oldDepthStencil);
            m_device->SetViewport(&m_oldViewport);

            SafeRelease(m_oldTarget);
            SafeRelease(m_oldDepthStencil);
            m_active = false;
            return result;
        }

    private:
        IDirect3DDevice9* m_device = nullptr;
        IDirect3DSurface9* m_depthStencil = nullptr;
        IDirect3DSurface9* m_oldTarget = nullptr;
        IDirect3DSurface9* m_oldDepthStencil = nullptr;
        D3DVIEWPORT9 m_oldViewport{};
        UINT m_width = 0;
        UINT m_height = 0;
        bool m_sceneBegun = false;
        bool m_active = false;
    };

    RenderToSurface* Renderer(void* renderer)
    {
        return static_cast<RenderToSurface*>(renderer);
    }

    bool LoadSurfaceLinear(IDirect3DDevice9* device, IDirect3DSurface9* source, IDirect3DSurface9* destination)
    {
        if (!device || !source || !destination)
            return false;

        D3DSURFACE_DESC destinationDesc{};
        if (FAILED(destination->GetDesc(&destinationDesc)))
            return false;

        // A regular D3DPOOL_DEFAULT texture surface is not a universally valid
        // StretchRect destination. R5's D3DXLoadSurfaceFromSurface handles that
        // case internally, so reproduce it with one RT-sized staging step:
        // source RT -> scaled RT -> SYSTEMMEM -> returned regular texture.
        IDirect3DTexture9* scaledTexture = nullptr;
        if (FAILED(
                device->CreateTexture(destinationDesc.Width, destinationDesc.Height, 1, D3DUSAGE_RENDERTARGET, destinationDesc.Format, D3DPOOL_DEFAULT, &scaledTexture, nullptr)))
        {
            return false;
        }

        bool copied = false;
        IDirect3DSurface9* scaledSurface = nullptr;
        scaledTexture->GetSurfaceLevel(0, &scaledSurface);

        if (scaledSurface && SUCCEEDED(device->StretchRect(source, nullptr, scaledSurface, nullptr, D3DTEXF_LINEAR)))
        {
            IDirect3DSurface9* systemSurface = nullptr;
            if (SUCCEEDED(device->CreateOffscreenPlainSurface(destinationDesc.Width, destinationDesc.Height, destinationDesc.Format, D3DPOOL_SYSTEMMEM, &systemSurface, nullptr)))
            {
                if (SUCCEEDED(device->GetRenderTargetData(scaledSurface, systemSurface)))
                    copied = SUCCEEDED(device->UpdateSurface(systemSurface, nullptr, destination, nullptr));

                SafeRelease(systemSurface);
            }
        }

        SafeRelease(scaledSurface);
        SafeRelease(scaledTexture);
        return copied;
    }
}

CLicensePlate::CLicensePlate(IDirect3DDevice9* device)
    : m_pDevice(device), m_pRenderer(nullptr), m_pTexture(nullptr), m_pSurface(nullptr), m_displayMode{}, m_pDefaultPlate(nullptr)
{
    // R5 +0x6D9B0 immediately creates its device resources.
    OnResetDevice();
}

CLicensePlate::~CLicensePlate()
{
    // +0x6D9E0 is only a tail-jump to +0x6D7B0.
    OnLostDevice();
}

void CLicensePlate::OnLostDevice()
{
    // Exact R5 +0x6D7B0 release order:
    //   +0x0C surface
    //   +0x08 texture
    //   +0x04 render-to-surface
    //   +0x20 default plate
    SafeRelease(m_pSurface);
    SafeRelease(m_pTexture);

    delete Renderer(m_pRenderer);
    m_pRenderer = nullptr;

    SafeRelease(m_pDefaultPlate);
}

void CLicensePlate::OnResetDevice()
{
    if (!m_pDevice)
        return;
    OnLostDevice();
    if (FAILED(m_pDevice->GetDisplayMode(0, &m_displayMode)))
        return;

    // R5 D3DXCreateTexture:
    //   128x32, 1 level, D3DUSAGE_RENDERTARGET,
    //   format = current display mode, D3DPOOL_DEFAULT.
    if (FAILED(m_pDevice->CreateTexture(128, 32, 1, D3DUSAGE_RENDERTARGET, m_displayMode.Format, D3DPOOL_DEFAULT, &m_pTexture, nullptr)))
    {
        return;
    }

    if (FAILED(m_pTexture->GetSurfaceLevel(0, &m_pSurface)))
    {
        OnLostDevice();
        return;
    }

    D3DSURFACE_DESC desc{};
    if (FAILED(m_pSurface->GetDesc(&desc)))
    {
        OnLostDevice();
        return;
    }

    auto* renderer = new RenderToSurface(m_pDevice, desc.Width, desc.Height);
    if (!renderer->IsValid())
    {
        delete renderer;
        OnLostDevice();
        return;
    }

    m_pRenderer = renderer;
}

IDirect3DTexture9* CLicensePlate::Create(const char* text)
{
    CFonts* fonts = g_fonts.get();

    // Exact +0x6D880 early-outs.
    if (!m_pDevice || !fonts || !m_pRenderer || !m_pTexture || !m_pSurface)
        return nullptr;

    IDirect3DTexture9* texture = nullptr;

    // R5 D3DXCreateTexture:
    //   64x32, 1 level, usage 0, current display format, D3DPOOL_DEFAULT.
    if (FAILED(m_pDevice->CreateTexture(64, 32, 1, 0, m_displayMode.Format, D3DPOOL_DEFAULT, &texture, nullptr)))
    {
        return nullptr;
    }

    auto* renderer = Renderer(m_pRenderer);

    if (FAILED(renderer->BeginScene(m_pSurface)))
    {
        renderer->EndScene();
        SafeRelease(texture);
        return nullptr;
    }

    const HRESULT clearResult = m_pDevice->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, DEFAULT_PLATE_BG_COLOR, 1.0f, 0);

    if (FAILED(clearResult))
    {
        renderer->EndScene();
        SafeRelease(texture);
        return nullptr;
    }

    // Exact rectangle built by +0x6D880.
    RECT rect{};
    rect.left = 0;
    rect.top = 3;
    rect.right = 128;
    rect.bottom = 32;

    fonts->DrawLicensePlateText(text, rect, DEFAULT_PLATE_TEXT_COLOR);

    // R5: ID3DXRenderToSurface::EndScene(0).
    renderer->EndScene();

    // R5 calls SetAutoGenFilterType(D3DTEXF_LINEAR) and ignores the result.
    texture->SetAutoGenFilterType(D3DTEXF_LINEAR);

    IDirect3DSurface9* destination = nullptr;
    if (FAILED(texture->GetSurfaceLevel(0, &destination)))
    {
        SafeRelease(texture);
        return nullptr;
    }

    // R5 uses D3DXLoadSurfaceFromSurface with D3DX_FILTER_LINEAR and color key 0
    // to scale its 128x32 working surface into the returned 64x32 regular
    // texture. Use the native no-D3DX equivalent path above. The copy
    // result must succeed before the texture is returned.
    const bool copied = LoadSurfaceLinear(m_pDevice, m_pSurface, destination);

    SafeRelease(destination);
    if (!copied)
        SafeRelease(texture);
    return texture;
}

RwTexture* CLicensePlate::CreateRwTexture()
{
    if (!m_pDevice || !m_pSurface)
        return nullptr;
    IDirect3DTexture9* scaled = nullptr;
    IDirect3DSurface9* surface = nullptr;
    IDirect3DSurface9* pixels = nullptr;
    RwTexture* result = nullptr;
    if (SUCCEEDED(m_pDevice->CreateTexture(64, 32, 1, D3DUSAGE_RENDERTARGET, m_displayMode.Format, D3DPOOL_DEFAULT, &scaled, nullptr)) &&
        SUCCEEDED(scaled->GetSurfaceLevel(0, &surface)) && SUCCEEDED(m_pDevice->StretchRect(m_pSurface, nullptr, surface, nullptr, D3DTEXF_LINEAR)) &&
        SUCCEEDED(m_pDevice->CreateOffscreenPlainSurface(64, 32, m_displayMode.Format, D3DPOOL_SYSTEMMEM, &pixels, nullptr)) &&
        SUCCEEDED(m_pDevice->GetRenderTargetData(surface, pixels)))
    {
        // gta normally uses X8R8G8B8; keep the conversion explicit for other display formats
        if (m_displayMode.Format == D3DFMT_X8R8G8B8 || m_displayMode.Format == D3DFMT_A8R8G8B8)
        {
            auto* raster = RwRasterCreate(64, 32, 32, rwRASTERTYPETEXTURE | rwRASTERFORMAT8888);
            if (raster)
            {
                auto* destination = RwRasterLock(raster, 0, rwRASTERLOCKWRITE);
                D3DLOCKED_RECT source{};
                if (destination && SUCCEEDED(pixels->LockRect(&source, nullptr, D3DLOCK_READONLY)))
                {
                    for (int row = 0; row < 32; ++row)
                    {
                        auto* output = destination + row * raster->stride;
                        std::memcpy(output, static_cast<const std::uint8_t*>(source.pBits) + row * source.Pitch, 64 * 4);
                        if (m_displayMode.Format == D3DFMT_X8R8G8B8)
                            for (int column = 0; column < 64; ++column)
                                output[column * 4 + 3] = 0xFF;
                    }
                    pixels->UnlockRect();
                    RwRasterUnlock(raster);
                    result = RwTextureCreate(raster);
                    if (result)
                        RwTextureSetFilterMode(result, rwFILTERLINEAR);
                }
                else if (destination)
                    RwRasterUnlock(raster);
                if (!result)
                    RwRasterDestroy(raster);
            }
        }
    }
    SafeRelease(pixels);
    SafeRelease(surface);
    SafeRelease(scaled);
    return result;
}
