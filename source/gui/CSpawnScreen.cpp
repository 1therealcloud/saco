#include <stdafx.h>

#if defined(LEGACY_SPAWN_SCREEN)

#   include "CSpawnScreen.h"
#   include "net/authentication.h"
#   include "CGui.h"

#   include "render/CFonts.h"

#   include <wincodec.h>

#   pragma comment(lib, "ole32.lib")
#   pragma comment(lib, "windowscodecs.lib")

namespace
{
    constexpr char kLogoPath[] = "logo.png";

    constexpr int kLeft = 20;
    constexpr int kBottomMargin = 30;
    constexpr int kLogoWidth = 185;
    constexpr int kLogoHeight = 73;
    constexpr int kLogoGap = 30;

    constexpr D3DCOLOR kTextColor = 0xFFFFFFFFu;
    constexpr D3DCOLOR kVersionColor = 0xFF27ABFAu;

    constexpr DWORD kFvf = D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1;

    struct Vertex
    {
        float x;
        float y;
        float z;
        float rhw;
        D3DCOLOR color;
        float u;
        float v;
    };

    template <typename T>
    void Release(T*& object)
    {
        if (!object)
            return;

        object->Release();
        object = nullptr;
    }

    bool ReadFileBytes(const char* path, std::vector<std::uint8_t>& bytes)
    {
        HANDLE file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
            return false;

        DWORD sizeHigh = 0;
        const DWORD size = GetFileSize(file, &sizeHigh);

        if (sizeHigh || !size || size > 64 * 1024 * 1024)
        {
            CloseHandle(file);
            return false;
        }

        bytes.resize(size);

        DWORD read = 0;
        const BOOL result = ReadFile(file, bytes.data(), size, &read, nullptr);
        CloseHandle(file);

        if (!result || read != size)
        {
            bytes.clear();
            return false;
        }

        return true;
    }

    bool DecodePng(const std::vector<std::uint8_t>& encoded, std::vector<std::uint8_t>& pixels, UINT& width, UINT& height)
    {
        width = 0;
        height = 0;

        if (encoded.empty())
            return false;

        const HRESULT init = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        const bool uninitialize = SUCCEEDED(init);

        IWICImagingFactory* factory = nullptr;
        IWICStream* stream = nullptr;
        IWICBitmapDecoder* decoder = nullptr;
        IWICBitmapFrameDecode* frame = nullptr;
        IWICFormatConverter* converter = nullptr;

        HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));

        if (SUCCEEDED(hr))
            hr = factory->CreateStream(&stream);

        if (SUCCEEDED(hr))
        {
            hr = stream->InitializeFromMemory(const_cast<BYTE*>(encoded.data()), static_cast<DWORD>(encoded.size()));
        }

        if (SUCCEEDED(hr))
            hr = factory->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnLoad, &decoder);

        if (SUCCEEDED(hr))
            hr = decoder->GetFrame(0, &frame);

        if (SUCCEEDED(hr))
            hr = factory->CreateFormatConverter(&converter);

        if (SUCCEEDED(hr))
        {
            hr = converter->Initialize(frame, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
        }

        if (SUCCEEDED(hr))
            hr = converter->GetSize(&width, &height);

        if (SUCCEEDED(hr) && width && height)
        {
            const std::uint64_t decodedSize = static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) * 4;

            if (decodedSize > 64 * 1024 * 1024)
            {
                hr = E_OUTOFMEMORY;
            }
            else
            {
                const UINT stride = width * 4;
                pixels.resize(static_cast<std::size_t>(decodedSize));

                hr = converter->CopyPixels(nullptr, stride, static_cast<UINT>(pixels.size()), pixels.data());
            }
        }

        Release(converter);
        Release(frame);
        Release(decoder);
        Release(stream);
        Release(factory);

        if (uninitialize)
            CoUninitialize();

        return SUCCEEDED(hr) && !pixels.empty();
    }
}

CSpawnScreen::CSpawnScreen(IDirect3DDevice9* device)
    : m_bEnabled(FALSE), m_szSpawnText(nullptr), m_pFont(nullptr), m_pDevice(device), m_pTexture(nullptr), m_pStateBlockSaved(nullptr), m_pStateBlockDraw(nullptr),
      m_pSprite(nullptr)
{
    if (m_pDevice)
        m_pFont = g_fonts.get();

    OnResetDevice();
}

CSpawnScreen::~CSpawnScreen()
{
    SetText(nullptr);
    OnLostDevice();
}

void CSpawnScreen::SetText(const char* text)
{
    char* replacement = nullptr;
    if (text)
    {
        const std::size_t length = std::strlen(text);
        replacement = new char[length + 1];
        std::memcpy(replacement, text, length + 1);
    }
    delete[] m_szSpawnText;
    m_szSpawnText = replacement;
}

void CSpawnScreen::OnResetDevice()
{
    if (m_pDevice)
    {
        Release(m_pTexture);
        Release(m_pStateBlockSaved);
        Release(m_pStateBlockDraw);

        LoadLogoTexture();
        CreateStateBlocks();
    }
}

void CSpawnScreen::OnLostDevice()
{
    Release(m_pTexture);
    Release(m_pStateBlockSaved);
    Release(m_pStateBlockDraw);
}

void CSpawnScreen::Draw()
{
    // r5 leaves the enabled check to the caller
    if (!m_pDevice || !m_pFont || !m_szSpawnText)
        return;

    SIZE size{};
    m_pFont->GetTextScreenSize(&size, m_szSpawnText, 0);

    D3DVIEWPORT9 viewport{};
    if (FAILED(m_pDevice->GetViewport(&viewport)))
        return;
    RECT rect{};
    rect.bottom = static_cast<LONG>(viewport.Y + viewport.Height) - kBottomMargin;
    rect.top = rect.bottom - size.cy;
    rect.left = static_cast<LONG>(viewport.X) + kLeft;
    rect.right = rect.left + size.cx;

    m_pFont->DrawText(m_szSpawnText, rect, kTextColor, true);

    rect.top -= kLogoHeight + kLogoGap;
    rect.bottom = rect.top + kLogoHeight;
    rect.left = static_cast<LONG>(viewport.X) + kLeft;
    rect.right = rect.left + kLogoWidth;

    if (m_pTexture && m_pStateBlockSaved && m_pStateBlockDraw && m_pDevice)
    {
        const float left = static_cast<float>(rect.left) - 0.5f;
        const float right = static_cast<float>(rect.right) + 0.5f;
        const float top = static_cast<float>(rect.top) - 0.5f;
        const float bottom = static_cast<float>(rect.bottom) + 0.5f;

        const Vertex vertices[4] = {
            {left, bottom, 0.1f, 1.0f, 0xFFFFFFFFu, 0.0f, 1.0f},
            {left, top, 0.1f, 1.0f, 0xFFFFFFFFu, 0.0f, 0.0f},
            {right, bottom, 0.1f, 1.0f, 0xFFFFFFFFu, 1.0f, 1.0f},
            {right, top, 0.1f, 1.0f, 0xFFFFFFFFu, 1.0f, 0.0f},
        };

        m_pStateBlockSaved->Capture();
        m_pStateBlockDraw->Apply();
        m_pDevice->SetTexture(0, m_pTexture);

        m_pDevice->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
        m_pDevice->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);

        m_pDevice->SetFVF(kFvf);
        m_pDevice->SetPixelShader(nullptr);
        m_pDevice->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, vertices, sizeof(Vertex));

        m_pStateBlockSaved->Apply();
    }

    const std::string versionText = "SA-MP " + std::string(authentication::client_version_string());
    m_pFont->GetTextScreenSize(&size, versionText.c_str(), 0);

    rect.top = rect.bottom;
    rect.bottom = rect.top + size.cy;
    rect.left = static_cast<LONG>(viewport.X) + kLeft + (kLogoWidth - size.cx) / 2;
    rect.right = rect.left + size.cx;

    m_pFont->DrawText(versionText.c_str(), rect, kVersionColor, true);
}

bool CSpawnScreen::LoadLogoTexture()
{
    if (!m_pDevice)
        return false;

    std::string path = CGui::GetAssetDirectory();
    if (!path.empty() && path.back() != '\\' && path.back() != '/')
        path += '\\';
    path += kLogoPath;
    std::vector<std::uint8_t> encoded;
    if (!ReadFileBytes(path.c_str(), encoded))
    {
        Log("[spawnscreen] failed to load logo.png from the game directory");
        return false;
    }

    std::vector<std::uint8_t> pixels;
    UINT width = 0;
    UINT height = 0;

    if (!DecodePng(encoded, pixels, width, height))
        return false;

    IDirect3DTexture9* texture = nullptr;

    if (FAILED(m_pDevice->CreateTexture(width, height, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &texture, nullptr)))
    {
        return false;
    }

    D3DLOCKED_RECT locked{};
    if (FAILED(texture->LockRect(0, &locked, nullptr, 0)))
    {
        texture->Release();
        return false;
    }

    for (UINT y = 0; y < height; ++y)
    {
        std::memcpy(static_cast<std::uint8_t*>(locked.pBits) + static_cast<std::size_t>(y) * locked.Pitch, pixels.data() + static_cast<std::size_t>(y) * width * 4,
                    static_cast<std::size_t>(width) * 4);
    }

    texture->UnlockRect(0);

    m_pTexture = texture;
    return true;
}

bool CSpawnScreen::CreateStateBlocks()
{
    if (!m_pDevice)
        return false;

    if (FAILED(m_pDevice->CreateStateBlock(D3DSBT_ALL, &m_pStateBlockSaved)))
        return false;
    {
        if (FAILED(m_pDevice->BeginStateBlock()))
            return false;

        m_pDevice->SetTexture(0, m_pTexture);

        m_pDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
        m_pDevice->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
        m_pDevice->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
        m_pDevice->SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);
        m_pDevice->SetRenderState(D3DRS_ALPHAREF, 0x08);
        m_pDevice->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_GREATEREQUAL);
        m_pDevice->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);
        m_pDevice->SetRenderState(D3DRS_CULLMODE, D3DCULL_CCW);
        m_pDevice->SetRenderState(D3DRS_STENCILENABLE, FALSE);
        m_pDevice->SetRenderState(D3DRS_CLIPPING, TRUE);
        m_pDevice->SetRenderState(D3DRS_CLIPPLANEENABLE, FALSE);
        m_pDevice->SetRenderState(D3DRS_VERTEXBLEND, D3DVBF_DISABLE);
        m_pDevice->SetRenderState(D3DRS_INDEXEDVERTEXBLENDENABLE, FALSE);
        m_pDevice->SetRenderState(D3DRS_FOGENABLE, FALSE);

        m_pDevice->SetRenderState(D3DRS_COLORWRITEENABLE, D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_BLUE | D3DCOLORWRITEENABLE_ALPHA);

        m_pDevice->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
        m_pDevice->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
        m_pDevice->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
        m_pDevice->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
        m_pDevice->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
        m_pDevice->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
        m_pDevice->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);
        m_pDevice->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);

        m_pDevice->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
        m_pDevice->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);

        m_pDevice->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
        m_pDevice->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
        m_pDevice->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);

        m_pDevice->SetVertexShader(nullptr);
        m_pDevice->SetPixelShader(nullptr);
        m_pDevice->SetFVF(kFvf);
        m_pDevice->SetRenderState(D3DRS_ZENABLE, FALSE);
        m_pDevice->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
        m_pDevice->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
        const HRESULT result = m_pDevice->EndStateBlock(&m_pStateBlockDraw);
        m_pStateBlockSaved->Apply();
        if (FAILED(result))
            return false;
    }

    return m_pStateBlockSaved && m_pStateBlockDraw;
}

#endif // LEGACY_SPAWN_SCREEN
