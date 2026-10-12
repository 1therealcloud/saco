/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * The original code is copyright (c) 2026, 1therealcloud.
 */

#include <stdafx.h>

#include "CGui.h"
#include "ime.h"

#include <windowsx.h>
#include <cmath>
#include <cwchar>
#include <cctype>
#include <iterator>
#include <wincodec.h>

namespace ui
{
    float opacity = 1.0f;

    void Appearance::SetVisible(bool visible, ULONGLONG now)
    {
        if (visible && !m_visible)
            m_started = now;
        m_visible = visible;
    }

    float Appearance::GetOpacity(ULONGLONG now) const
    {
#ifdef UI_FADE
        const float progress = std::min(1.0f, static_cast<float>(now - m_started) / 150.0f);
        return m_visible ? progress * progress * (3.0f - 2.0f * progress) : 0.0f;
#else
        return 1.0f;
#endif
    }

    OpacityScope::OpacityScope(float value) : m_previous(opacity)
    {
        opacity *= value;
    }

    OpacityScope::~OpacityScope()
    {
        opacity = m_previous;
    }

    DWORD FadeColor(DWORD color)
    {
        return (color & 0xFFFFFF) | (static_cast<DWORD>((color >> 24) * opacity + 0.5f) << 24);
    }
}

namespace cgui
{
    namespace
    {
        constexpr D3DCOLOR kScoreboardBackground = 0x960A0A0Au;
        constexpr D3DCOLOR kDialogBackground = 0xDC050505u;
        constexpr D3DCOLOR kTextColor = 0xFFFFFFFFu;
        constexpr D3DCOLOR kDisabledTextColor = 0xFFC8C8C8u;
        constexpr int kDefaultFontHeight = 20;
        constexpr int kDefaultFontWeight = FW_BOLD;
        constexpr unsigned char kPasswordGlyph = 0x1F; // reserved atlas slot for the password bullet

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

        constexpr DWORD kFvf = D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1;

        struct FontGlyph
        {
            float u0 = 0.0f;
            float v0 = 0.0f;
            float u1 = 0.0f;
            float v1 = 0.0f;
            int width = 0;
            int height = 0;
            int advance = 0;
        };

        struct FontAtlas
        {
            IDirect3DTexture9* texture = nullptr;
            std::array<FontGlyph, 256> glyphs{};
            int height = kDefaultFontHeight;
            UINT width = 0;
            UINT textureHeight = 0;
        };

        IDirect3DDevice9* g_device = nullptr;
        HWND g_window = nullptr;
        RECT g_clientRect{};
        bool g_clientDirty = true;
        IDirect3DTexture9* g_skin = nullptr;
        IDirect3DTexture9* g_cursor = nullptr;
        struct TextureMetrics
        {
            float inverseWidth = 0;
            float inverseHeight = 0;
        };
        TextureMetrics g_skinMetrics{}, g_cursorMetrics{};
        FontAtlas g_font{};
        std::vector<Vertex> g_textVertices;
        std::size_t g_vertexChunkSize = 0;
        IDirect3DVertexBuffer9* g_vertexBuffer = nullptr;
        std::size_t g_vertexBufferOffset = 0;
        constexpr std::size_t kVertexBufferCapacity = 24576;
        std::array<Dialog, static_cast<std::size_t>(DialogId::Count)> g_dialogs{};
        std::array<std::vector<SkinElement>, 10> g_defaultElements{};
        std::string g_assetDirectory;
        std::string g_fontFace = "Arial";
        int g_fontHeight = kDefaultFontHeight;
        int g_fontWeight = kDefaultFontWeight;
        bool g_fontDirty = false;
        unsigned int g_fontRevision = 0;
        HDC g_measureDc = nullptr;
        HFONT g_measureFont = nullptr;
        HGDIOBJ g_measureOldFont = nullptr;
        void ReleaseMeasurement()
        {
            ZoneScoped;
            if (g_measureDc && g_measureOldFont)
                SelectObject(g_measureDc, g_measureOldFont);
            if (g_measureFont)
                DeleteObject(g_measureFont);
            if (g_measureDc)
                DeleteDC(g_measureDc);
            g_measureDc = nullptr;
            g_measureFont = nullptr;
            g_measureOldFont = nullptr;
        }
        HDC MeasurementDc()
        {
            ZoneScoped;
            if (!g_measureDc)
            {
                g_measureDc = CreateCompatibleDC(nullptr);
                g_measureFont = CreateFontA(g_fontHeight, 0, 0, 0, g_fontWeight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                                            DEFAULT_PITCH, g_fontFace.c_str());
                if (!g_measureDc || !g_measureFont)
                {
                    ReleaseMeasurement();
                    return nullptr;
                }
                g_measureOldFont = SelectObject(g_measureDc, g_measureFont);
            }
            return g_measureDc;
        }

        bool g_initialized = false;
        std::vector<IDirect3DStateBlock9*> g_states;
        std::size_t g_passDepth = 0;
        bool g_uiStateValid = false;
        IDirect3DTexture9* g_boundTexture = nullptr;
        IDirect3DTexture9* g_pendingTexture = nullptr;
        std::vector<Vertex> g_pendingVertices;
        RECT g_pendingClip{};
        bool g_pendingClipped = false;
        D3DVIEWPORT9 g_passViewport{};
        std::unordered_map<IDirect3DTexture9*, TextureMetrics> g_textureDescriptions;
        void FlushUi();
        void SubmitUi(const Vertex* vertices, std::size_t count, IDirect3DTexture9* texture, const RECT* clip = nullptr);

        std::size_t Index(DialogId id)
        {
            return static_cast<std::size_t>(id);
        }

        int Width(const RECT& rect)
        {
            return rect.right - rect.left;
        }

        int Height(const RECT& rect)
        {
            return rect.bottom - rect.top;
        }

        bool PtIn(const RECT& rect, int x, int y)
        {
            return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
        }

        std::string AssetPath(const char* name)
        {
            ZoneScoped;
            if (!name)
                return {};
            if (g_assetDirectory.empty())
                return name;
            const char last = g_assetDirectory.back();
            if (last == '\\' || last == '/')
                return g_assetDirectory + name;
            return g_assetDirectory + "\\" + name;
        }

        template <typename T>
        void Release(T*& object)
        {
            if (object)
            {
                object->Release();
                object = nullptr;
            }
        }

        bool DecodePng(const char* path, std::vector<std::uint8_t>& pixels, UINT& width, UINT& height)
        {
            ZoneScoped;
            pixels.clear();
            width = 0;
            height = 0;
            if (!path || !*path)
                return false;

            HANDLE file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file == INVALID_HANDLE_VALUE)
                return false;

            DWORD sizeHigh = 0;
            const DWORD size = GetFileSize(file, &sizeHigh);
            if (sizeHigh != 0 || size == 0 || size > 64 * 1024 * 1024)
            {
                CloseHandle(file);
                return false;
            }

            std::vector<std::uint8_t> encoded(size);
            DWORD read = 0;
            const bool readOk = ReadFile(file, encoded.data(), static_cast<DWORD>(encoded.size()), &read, nullptr) != FALSE && read == encoded.size();
            CloseHandle(file);
            if (!readOk)
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
                hr = stream->InitializeFromMemory(encoded.data(), static_cast<DWORD>(encoded.size()));
            if (SUCCEEDED(hr))
                hr = factory->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnLoad, &decoder);
            if (SUCCEEDED(hr))
                hr = decoder->GetFrame(0, &frame);
            if (SUCCEEDED(hr))
                hr = factory->CreateFormatConverter(&converter);
            if (SUCCEEDED(hr))
                hr = converter->Initialize(frame, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
            if (SUCCEEDED(hr))
                hr = converter->GetSize(&width, &height);

            if (SUCCEEDED(hr) && width && height)
            {
                const std::uint64_t decodedSize = static_cast<std::uint64_t>(width) * height * 4;
                if (decodedSize > 64 * 1024 * 1024)
                    hr = E_OUTOFMEMORY;
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

        bool CreateTextureFromPng(const char* path, IDirect3DTexture9** texture)
        {
            ZoneScoped;
            if (!g_device || !texture)
                return false;
            *texture = nullptr;

            std::vector<std::uint8_t> pixels;
            UINT width = 0;
            UINT height = 0;
            if (!DecodePng(path, pixels, width, height))
                return false;

            IDirect3DTexture9* result = nullptr;
            if (FAILED(g_device->CreateTexture(width, height, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &result, nullptr)))
                return false;

            D3DLOCKED_RECT locked{};
            if (FAILED(result->LockRect(0, &locked, nullptr, 0)))
            {
                result->Release();
                return false;
            }

            for (UINT y = 0; y < height; ++y)
                std::memcpy(static_cast<std::uint8_t*>(locked.pBits) + y * locked.Pitch, pixels.data() + static_cast<std::size_t>(y) * width * 4,
                            static_cast<std::size_t>(width) * 4);

            result->UnlockRect(0);
            *texture = result;
            return true;
        }

        bool CreateCursorTexture(const char* path)
        {
            ZoneScoped;
            Release(g_cursor);
            if (!CreateTextureFromPng(path, &g_cursor))
                return false;
            D3DSURFACE_DESC desc{};
            if (FAILED(g_cursor->GetLevelDesc(0, &desc)) || desc.Width != 32 || desc.Height != 32)
            {
                Log("[gui] unexpected cursor dimensions: %ux%u", desc.Width, desc.Height);
                Release(g_cursor);
                return false;
            }
            return true;
        }

        UINT NextPowerOfTwo(UINT value)
        {
            UINT result = 1;
            while (result < value && result < 0x80000000u)
                result <<= 1;
            return result;
        }

        bool CreateFontAtlas()
        {
            ZoneScoped;
            if (!g_device)
                return false;

            HDC dc = CreateCompatibleDC(nullptr);
            if (!dc)
                return false;

            HFONT font = CreateFontA(g_fontHeight, 0, 0, 0, g_fontWeight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                                     DEFAULT_PITCH | FF_DONTCARE, g_fontFace.c_str());
            if (!font)
            {
                DeleteDC(dc);
                return false;
            }

            HGDIOBJ oldFont = SelectObject(dc, font);
            TEXTMETRICA metrics{};
            if (!GetTextMetricsA(dc, &metrics))
            {
                SelectObject(dc, oldFont);
                DeleteObject(font);
                DeleteDC(dc);
                return false;
            }

            constexpr int cols = 16;
            constexpr int rows = 16;
            int maxWidth = std::max<int>(1, metrics.tmAveCharWidth);
            std::array<SIZE, 256> sizes{};

            for (int c = 0; c < 256; ++c)
            {
                if (c < 32 && c != kPasswordGlyph)
                {
                    sizes[static_cast<std::size_t>(c)].cx = metrics.tmAveCharWidth;
                    sizes[static_cast<std::size_t>(c)].cy = metrics.tmHeight;
                    continue;
                }

                const char text[2]{static_cast<char>(c), 0};
                SIZE size{};
                const BOOL measured = GetTextExtentPoint32A(dc, text, 1, &size);
                if (c == kPasswordGlyph)
                    size.cx = size.cy = metrics.tmHeight;
                if (!measured)
                {
                    size.cx = metrics.tmAveCharWidth;
                    size.cy = metrics.tmHeight;
                }
                sizes[static_cast<std::size_t>(c)] = size;
                maxWidth = std::max<int>(maxWidth, size.cx);
            }

            const int glyphHeight = std::max<int>(1, metrics.tmHeight);
            const int cellW = maxWidth + 2;
            const int cellH = glyphHeight + 2;
            const UINT texW = NextPowerOfTwo(static_cast<UINT>(cellW * cols));
            const UINT texH = NextPowerOfTwo(static_cast<UINT>(cellH * rows));

            BITMAPINFO bmi{};
            bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bmi.bmiHeader.biWidth = static_cast<LONG>(texW);
            bmi.bmiHeader.biHeight = -static_cast<LONG>(texH);
            bmi.bmiHeader.biPlanes = 1;
            bmi.bmiHeader.biBitCount = 32;
            bmi.bmiHeader.biCompression = BI_RGB;

            void* bits = nullptr;
            HBITMAP bitmap = CreateDIBSection(dc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
            if (!bitmap || !bits)
            {
                if (bitmap)
                    DeleteObject(bitmap);
                SelectObject(dc, oldFont);
                DeleteObject(font);
                DeleteDC(dc);
                return false;
            }

            HGDIOBJ oldBitmap = SelectObject(dc, bitmap);
            SetBkColor(dc, RGB(0, 0, 0));
            SetTextColor(dc, RGB(255, 255, 255));
            SetBkMode(dc, OPAQUE);
            PatBlt(dc, 0, 0, static_cast<int>(texW), static_cast<int>(texH), BLACKNESS);

            std::array<FontGlyph, 256> glyphs{};
            for (int c = 0; c < 256; ++c)
            {
                const int col = c % cols;
                const int row = c / cols;
                const int x = col * cellW;
                const int y = row * cellH;
                const SIZE size = sizes[static_cast<std::size_t>(c)];
                const int drawWidth = std::clamp<int>(size.cx, 1, maxWidth);

                if (c == kPasswordGlyph)
                {
                    HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(WHITE_BRUSH));
                    HGDIOBJ oldPen = SelectObject(dc, GetStockObject(NULL_PEN));
                    Ellipse(dc, x + 1, y + 1, x + 1 + glyphHeight, y + 1 + glyphHeight);
                    SelectObject(dc, oldPen);
                    SelectObject(dc, oldBrush);
                }
                else if (c >= 32)
                {
                    const char text[2]{static_cast<char>(c), 0};
                    TextOutA(dc, x + 1, y + 1, text, 1);
                }

                FontGlyph& glyph = glyphs[static_cast<std::size_t>(c)];
                glyph.u0 = static_cast<float>(x + 1) / static_cast<float>(texW);
                glyph.v0 = static_cast<float>(y + 1) / static_cast<float>(texH);
                glyph.u1 = static_cast<float>(x + 1 + drawWidth) / static_cast<float>(texW);
                glyph.v1 = static_cast<float>(y + 1 + glyphHeight) / static_cast<float>(texH);
                glyph.width = drawWidth;
                glyph.height = glyphHeight;
                glyph.advance = std::max<int>(1, size.cx);
            }

            auto* pixels = static_cast<std::uint32_t*>(bits);
            const std::size_t pixelCount = static_cast<std::size_t>(texW) * texH;
            for (std::size_t i = 0; i < pixelCount; ++i)
            {
                const std::uint8_t b = static_cast<std::uint8_t>(pixels[i] & 0xFF);
                const std::uint8_t g = static_cast<std::uint8_t>((pixels[i] >> 8) & 0xFF);
                const std::uint8_t r = static_cast<std::uint8_t>((pixels[i] >> 16) & 0xFF);
                const std::uint8_t alpha = std::max({r, g, b});
                pixels[i] = static_cast<std::uint32_t>(alpha) << 24 | 0x00FFFFFFu;
            }

            IDirect3DTexture9* texture = nullptr;
            HRESULT hr = g_device->CreateTexture(texW, texH, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &texture, nullptr);
            if (SUCCEEDED(hr))
            {
                D3DLOCKED_RECT locked{};
                hr = texture->LockRect(0, &locked, nullptr, 0);
                if (SUCCEEDED(hr))
                {
                    for (UINT y = 0; y < texH; ++y)
                    {
                        std::memcpy(static_cast<std::uint8_t*>(locked.pBits) + y * locked.Pitch, static_cast<std::uint8_t*>(bits) + static_cast<std::size_t>(y) * texW * 4,
                                    static_cast<std::size_t>(texW) * 4);
                    }
                    texture->UnlockRect(0);
                }
            }

            SelectObject(dc, oldBitmap);
            SelectObject(dc, oldFont);
            DeleteObject(bitmap);
            DeleteObject(font);
            DeleteDC(dc);

            if (FAILED(hr))
            {
                Release(texture);
                return false;
            }

            Release(g_font.texture);
            g_font.texture = texture;
            g_font.glyphs = glyphs;
            g_font.height = glyphHeight;
            g_font.width = texW;
            g_font.textureHeight = texH;
            g_fontDirty = false;
            ++g_fontRevision;
            ReleaseMeasurement();
            return true;
        }

        class StateGuard
        {
        public:
            StateGuard()
            {
                ZoneScoped;
                FlushUi();
                m_previousViewport = g_passViewport;
                const auto index = g_passDepth++;
                if (index == g_states.size())
                    g_states.push_back(nullptr);
                auto& state = g_states[index];
                if (!state && g_device)
                    g_device->CreateStateBlock(D3DSBT_ALL, &state);
                if (state && SUCCEEDED(state->Capture()))
                    m_state = state;
                g_uiStateValid = false;
                g_device->GetViewport(&g_passViewport);
                g_textureDescriptions.clear();
            }
            ~StateGuard()
            {
                FlushUi();
                if (m_state)
                    m_state->Apply();
                --g_passDepth;
                g_passViewport = m_previousViewport;
                g_uiStateValid = false;
            }
            bool IsValid() const
            {
                return m_state != nullptr;
            }

        private:
            D3DVIEWPORT9 m_previousViewport{};
            IDirect3DStateBlock9* m_state = nullptr;
        };

        void ApplyUiState(IDirect3DTexture9* texture)
        {
            ZoneScoped;
            if (g_passDepth && g_uiStateValid && g_boundTexture == texture)
                return;
            const bool baseline = !g_passDepth || !g_uiStateValid;
            g_boundTexture = texture;
            g_uiStateValid = true;
            if (baseline)
            {
                g_device->SetVertexShader(nullptr);
                g_device->SetPixelShader(nullptr);
                g_device->SetFVF(kFvf);
                g_device->SetRenderState(D3DRS_ZENABLE, FALSE);
                g_device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
                g_device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
                g_device->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, FALSE);
                g_device->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
                g_device->SetRenderState(D3DRS_COLORWRITEENABLE, 0x0F);
                g_device->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
                g_device->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
                g_device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
                g_device->SetRenderState(D3DRS_FOGENABLE, FALSE);
                g_device->SetRenderState(D3DRS_STENCILENABLE, FALSE);
                g_device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
                g_device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
                g_device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
                g_device->SetRenderState(D3DRS_LIGHTING, FALSE);
                g_device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
            }
            g_device->SetTexture(0, texture);
            g_device->SetTextureStageState(0, D3DTSS_COLOROP, texture ? D3DTOP_MODULATE : D3DTOP_SELECTARG1);
            g_device->SetTextureStageState(0, D3DTSS_COLORARG1, texture ? D3DTA_TEXTURE : D3DTA_DIFFUSE);
            g_device->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
            g_device->SetTextureStageState(0, D3DTSS_ALPHAOP, texture ? D3DTOP_MODULATE : D3DTOP_SELECTARG1);
            g_device->SetTextureStageState(0, D3DTSS_ALPHAARG1, texture ? D3DTA_TEXTURE : D3DTA_DIFFUSE);
            g_device->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
            if (baseline)
            {
                g_device->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
                g_device->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
                g_device->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
                g_device->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
                g_device->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
            }
        }

        void FlushUi()
        {
            ZoneScoped;
            if (g_pendingVertices.empty())
                return;
            ApplyUiState(g_pendingTexture);
            if (g_pendingClipped)
            {
                g_device->SetScissorRect(&g_pendingClip);
                g_device->SetRenderState(D3DRS_SCISSORTESTENABLE, TRUE);
            }
            if (!g_vertexBuffer)
            {
                g_vertexBufferOffset = 0;
                g_device->CreateVertexBuffer(static_cast<UINT>(kVertexBufferCapacity * sizeof(Vertex)), D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, kFvf, D3DPOOL_DEFAULT,
                                             &g_vertexBuffer, nullptr);
            }
            for (std::size_t first = 0; first < g_pendingVertices.size(); first += g_vertexChunkSize)
            {
                const std::size_t size = std::min(g_vertexChunkSize, g_pendingVertices.size() - first);
                if (g_vertexBuffer)
                {
                    if (g_vertexBufferOffset + size > kVertexBufferCapacity)
                        g_vertexBufferOffset = 0;
                    void* output = nullptr;
                    if (SUCCEEDED(g_vertexBuffer->Lock(static_cast<UINT>(g_vertexBufferOffset * sizeof(Vertex)), static_cast<UINT>(size * sizeof(Vertex)), &output,
                                                       g_vertexBufferOffset ? D3DLOCK_NOOVERWRITE : D3DLOCK_DISCARD)))
                    {
                        std::memcpy(output, g_pendingVertices.data() + first, size * sizeof(Vertex));
                        g_vertexBuffer->Unlock();
                        g_device->SetStreamSource(0, g_vertexBuffer, 0, sizeof(Vertex));
                        g_device->DrawPrimitive(D3DPT_TRIANGLELIST, static_cast<UINT>(g_vertexBufferOffset), static_cast<UINT>(size / 3));

                        g_vertexBufferOffset += size;
                        continue;
                    }
                }
                g_device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, static_cast<UINT>(size / 3), g_pendingVertices.data() + first, sizeof(Vertex));

            }
            if (g_pendingClipped)
                g_device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
            g_pendingVertices.clear();
        }

        void SubmitUi(const Vertex* vertices, std::size_t count, IDirect3DTexture9* texture, const RECT* clip)
        {
            ZoneScoped;
            if (!g_pendingVertices.empty() && (texture != g_pendingTexture || (clip != nullptr) != g_pendingClipped || (clip && !EqualRect(clip, &g_pendingClip))))
                FlushUi();
            g_pendingTexture = texture;
            g_pendingClipped = clip != nullptr;
            if (clip)
                g_pendingClip = *clip;
            g_pendingVertices.insert(g_pendingVertices.end(), vertices, vertices + count);
            if (!g_passDepth || g_pendingVertices.size() >= kVertexBufferCapacity)
                FlushUi();
        }

        void DrawQuad(const RECT& rect, D3DCOLOR color, IDirect3DTexture9* texture = nullptr, const RECT* textureRect = nullptr)
        {
            ZoneScoped;
            color = ui::FadeColor(color);
            if (!g_device || Width(rect) <= 0 || Height(rect) <= 0)
                return;

            float u0 = 0.0f, v0 = 0.0f, u1 = 1.0f, v1 = 1.0f;
            if (texture && textureRect)
            {
                auto& metrics = texture == g_skin ? g_skinMetrics : texture == g_cursor ? g_cursorMetrics : g_textureDescriptions[texture];
                if (!metrics.inverseWidth)
                {
                    D3DSURFACE_DESC desc{};
                    if (SUCCEEDED(texture->GetLevelDesc(0, &desc)) && desc.Width && desc.Height)
                    {
                        metrics.inverseWidth = 1.0f / desc.Width;
                        metrics.inverseHeight = 1.0f / desc.Height;
                    }
                }
                if (metrics.inverseWidth)
                {
                    u0 = static_cast<float>(textureRect->left) * metrics.inverseWidth;
                    v0 = static_cast<float>(textureRect->top) * metrics.inverseHeight;
                    u1 = static_cast<float>(textureRect->right) * metrics.inverseWidth;
                    v1 = static_cast<float>(textureRect->bottom) * metrics.inverseHeight;
                }
            }

            const float l = static_cast<float>(rect.left) - 0.5f;
            const float t = static_cast<float>(rect.top) - 0.5f;
            const float r = static_cast<float>(rect.right) - 0.5f;
            const float b = static_cast<float>(rect.bottom) - 0.5f;
            const Vertex vertices[4] = {
                {l, t, 0.0f, 1.0f, color, u0, v0},
                {r, t, 0.0f, 1.0f, color, u1, v0},
                {l, b, 0.0f, 1.0f, color, u0, v1},
                {r, b, 0.0f, 1.0f, color, u1, v1},
            };
            const Vertex triangles[]{vertices[0], vertices[1], vertices[2], vertices[2], vertices[1], vertices[3]};
            SubmitUi(triangles, 6, texture);
        }

        void DrawGradientQuad(const RECT& rect, D3DCOLOR topLeft, D3DCOLOR topRight, D3DCOLOR bottomLeft, D3DCOLOR bottomRight)
        {
            ZoneScoped;
            topLeft = ui::FadeColor(topLeft);
            topRight = ui::FadeColor(topRight);
            bottomLeft = ui::FadeColor(bottomLeft);
            bottomRight = ui::FadeColor(bottomRight);
            if (!g_device || Width(rect) <= 0 || Height(rect) <= 0)
                return;

            const float l = static_cast<float>(rect.left) - 0.5f;
            const float t = static_cast<float>(rect.top) - 0.5f;
            const float r = static_cast<float>(rect.right) - 0.5f;
            const float b = static_cast<float>(rect.bottom) - 0.5f;
            const Vertex vertices[4] = {
                {l, t, 0.0f, 1.0f, topLeft, 0.0f, 0.0f},
                {r, t, 0.0f, 1.0f, topRight, 1.0f, 0.0f},
                {l, b, 0.0f, 1.0f, bottomLeft, 0.0f, 1.0f},
                {r, b, 0.0f, 1.0f, bottomRight, 1.0f, 1.0f},
            };
            const Vertex triangles[]{vertices[0], vertices[1], vertices[2], vertices[2], vertices[1], vertices[3]};
            SubmitUi(triangles, 6, nullptr);
        }

        D3DCOLOR Modulate(D3DCOLOR color, float factor)
        {
            const auto channel = [factor](unsigned int value) {
                return static_cast<unsigned int>(std::clamp(value * factor, 0.0f, 255.0f));
            };
            return (channel((color >> 24) & 0xFF) << 24) | (channel((color >> 16) & 0xFF) << 16) | (channel((color >> 8) & 0xFF) << 8) | channel(color & 0xFF);
        }

        D3DCOLOR StateColor(D3DCOLOR base, ControlState state)
        {
            ZoneScoped;
            // original default state colors keep normal/focus/hover/pressed unchanged,
            // use 0xC8808080 for disabled and 0 for hidden unless a control overrides it.
            switch (state)
            {
                case ControlState::Disabled:
                    return 0xC8808080u;
                case ControlState::Hidden:
                    return 0x00000000u;
                default:
                    return base;
            }
        }

        D3DCOLOR ElementFontColor(ControlType type, unsigned int element, ControlState state)
        {
            ZoneScoped;
            const SkinElement* skin = GetDefaultElement(type, element);
            if (!skin)
                return state == ControlState::Disabled ? kDisabledTextColor : kTextColor;
            if (state == ControlState::Disabled)
                return kDisabledTextColor;
            if (state == ControlState::Hidden)
                return 0;
            return skin->fontColor;
        }

        DWORD ElementTextFormat(ControlType type, unsigned int element, DWORD fallback)
        {
            if (const SkinElement* skin = GetDefaultElement(type, element))
                return skin->textFormat;
            return fallback;
        }

        void DrawSkin(ControlType type, unsigned int element, const RECT& destination, ControlState state)
        {
            ZoneScoped;
            const SkinElement* skin = GetDefaultElement(type, element);
            if (!skin || !g_skin)
            {
                DrawQuad(destination, StateColor(0xD0505050u, state));
                return;
            }
            DrawQuad(destination, StateColor(skin->textureColor, state), g_skin, &skin->texture);
        }

        void DrawNineSlice(ControlType type, const RECT& destination, ControlState state)
        {
            ZoneScoped;
            const SkinElement* center = GetDefaultElement(type, 0);
            const SkinElement* tl = GetDefaultElement(type, 1);
            const SkinElement* top = GetDefaultElement(type, 2);
            const SkinElement* tr = GetDefaultElement(type, 3);
            const SkinElement* left = GetDefaultElement(type, 4);
            const SkinElement* right = GetDefaultElement(type, 5);
            const SkinElement* bl = GetDefaultElement(type, 6);
            const SkinElement* bottom = GetDefaultElement(type, 7);
            const SkinElement* br = GetDefaultElement(type, 8);
            if (!g_skin || !center || !tl || !top || !tr || !left || !right || !bl || !bottom || !br)
            {
                DrawSkin(type, 0, destination, state);
                return;
            }

            const int lw = Width(tl->texture);
            const int rw = Width(tr->texture);
            const int th = Height(tl->texture);
            const int bh = Height(bl->texture);
            const int innerLeft = std::min(destination.right, destination.left + lw);
            const int innerRight = std::max<int>(innerLeft, destination.right - rw);
            const int innerTop = std::min(destination.bottom, destination.top + th);
            const int innerBottom = std::max<int>(innerTop, destination.bottom - bh);
            const D3DCOLOR color = StateColor(center->textureColor, state);

            DrawQuad(RECT{destination.left, destination.top, innerLeft, innerTop}, color, g_skin, &tl->texture);
            DrawQuad(RECT{innerLeft, destination.top, innerRight, innerTop}, color, g_skin, &top->texture);
            DrawQuad(RECT{innerRight, destination.top, destination.right, innerTop}, color, g_skin, &tr->texture);
            DrawQuad(RECT{destination.left, innerTop, innerLeft, innerBottom}, color, g_skin, &left->texture);
            DrawQuad(RECT{innerLeft, innerTop, innerRight, innerBottom}, color, g_skin, &center->texture);
            DrawQuad(RECT{innerRight, innerTop, destination.right, innerBottom}, color, g_skin, &right->texture);
            DrawQuad(RECT{destination.left, innerBottom, innerLeft, destination.bottom}, color, g_skin, &bl->texture);
            DrawQuad(RECT{innerLeft, innerBottom, innerRight, destination.bottom}, color, g_skin, &bottom->texture);
            DrawQuad(RECT{innerRight, innerBottom, destination.right, destination.bottom}, color, g_skin, &br->texture);
        }

        std::size_t NextCharacter(const char* text, std::size_t position)
        {
            ZoneScoped;
            const unsigned char value = static_cast<unsigned char>(text[position]);
            std::size_t length = 1;
            if (GetACP() == CP_UTF8)
                length = value >= 0xF0 ? 4 : value >= 0xE0 ? 3 : value >= 0xC0 ? 2 : 1;
            else if (IsDBCSLeadByteEx(CP_ACP, value))
                length = 2;
            for (std::size_t i = 1; i < length; ++i)
                if (!text[position + i])
                    return position + i;
            return position + length;
        }

        std::size_t CharacterBoundary(const char* text, std::size_t position)
        {
            ZoneScoped;
            std::size_t current = 0;
            while (text[current] && current < position)
            {
                const std::size_t next = NextCharacter(text, current);
                if (next > position)
                    break;
                current = next;
            }
            return current;
        }

        std::size_t CharacterCount(const char* text, std::size_t bytes)
        {
            std::size_t count = 0;
            for (std::size_t i = 0; text[i] && i < bytes; i = NextCharacter(text, i))
                ++count;
            return count;
        }

        bool HasMultibyteText(const char* text)
        {
            for (std::size_t i = 0; text[i]; ++i)
                if ((GetACP() == CP_UTF8 && static_cast<unsigned char>(text[i]) >= 0x80) || IsDBCSLeadByteEx(CP_ACP, static_cast<unsigned char>(text[i])))
                    return true;
            return false;
        }

        bool CreateTextTexture(const char* text, IDirect3DTexture9** output, SIZE& extent)
        {
            ZoneScoped;
            HDC dc = CreateCompatibleDC(nullptr);
            if (!dc)
                return false;
            HFONT font = CreateFontA(g_fontHeight, 0, 0, 0, g_fontWeight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                                     DEFAULT_PITCH, g_fontFace.c_str());
            if (!font)
            {
                DeleteDC(dc);
                return false;
            }
            HGDIOBJ oldFont = SelectObject(dc, font);
            GetTextExtentPoint32A(dc, text, static_cast<int>(std::strlen(text)), &extent);
            const UINT width = NextPowerOfTwo(std::max<LONG>(1, extent.cx));
            const UINT height = NextPowerOfTwo(std::max<LONG>(1, extent.cy));
            BITMAPINFO info{};
            info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth = width;
            info.bmiHeader.biHeight = -static_cast<LONG>(height);
            info.bmiHeader.biPlanes = 1;
            info.bmiHeader.biBitCount = 32;
            void* pixels = nullptr;
            HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
            bool success = false;
            if (bitmap && pixels)
            {
                HGDIOBJ oldBitmap = SelectObject(dc, bitmap);
                std::memset(pixels, 0, width * height * 4);
                SetBkMode(dc, TRANSPARENT);
                SetTextColor(dc, RGB(255, 255, 255));
                TextOutA(dc, 0, 0, text, static_cast<int>(std::strlen(text)));
                GdiFlush();
                auto* colors = static_cast<DWORD*>(pixels);
                for (UINT i = 0; i < width * height; ++i)
                {
                    const DWORD alpha = std::max({colors[i] & 0xFF, (colors[i] >> 8) & 0xFF, (colors[i] >> 16) & 0xFF});
                    colors[i] = (alpha << 24) | 0xFFFFFF;
                }
                IDirect3DTexture9* texture = nullptr;
                if (SUCCEEDED(g_device->CreateTexture(width, height, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &texture, nullptr)))
                {
                    D3DLOCKED_RECT locked{};
                    if (SUCCEEDED(texture->LockRect(0, &locked, nullptr, 0)))
                    {
                        for (UINT y = 0; y < height; ++y)
                            std::memcpy(static_cast<BYTE*>(locked.pBits) + y * locked.Pitch, colors + y * width, width * 4);
                        texture->UnlockRect(0);
                        FlushUi();
                        g_textureDescriptions.erase(*output);
                        Release(*output);
                        *output = texture;
                        success = true;
                    }
                    else
                        texture->Release();
                }
                SelectObject(dc, oldBitmap);
            }
            if (bitmap)
                DeleteObject(bitmap);
            SelectObject(dc, oldFont);
            DeleteObject(font);
            DeleteDC(dc);
            return success;
        }

        int MeasureText(const char* text, std::size_t count = static_cast<std::size_t>(-1), int passwordSize = 0)
        {
            ZoneScoped;
            if (!text)
                return 0;
            if (!passwordSize && HasMultibyteText(text))
            {
                if (HDC dc = MeasurementDc())
                {
                    SIZE size{};
                    if (GetTextExtentPoint32A(dc, text, static_cast<int>(std::min(count, std::strlen(text))), &size))
                        return size.cx;
                }
            }
            int width = 0;
            for (std::size_t i = 0; text[i] && i < count; ++i)
                width += passwordSize ? passwordSize + 2 : g_font.glyphs[static_cast<unsigned char>(text[i])].advance;
            return width;
        }

        bool ReadColorTag(const char* text, D3DCOLOR& color)
        {
            ZoneScoped;
            if (*text != '{')
                return false;
            D3DCOLOR rgb = 0;
            for (int digit = 1; digit <= 6; ++digit)
            {
                const char c = text[digit];
                int value = -1;
                if (c >= '0' && c <= '9')
                    value = c - '0';
                else if (c >= 'a' && c <= 'f')
                    value = c - 'a' + 10;
                else if (c >= 'A' && c <= 'F')
                    value = c - 'A' + 10;
                if (value < 0)
                    return false;
                rgb = (rgb << 4) | static_cast<D3DCOLOR>(value);
            }
            if (text[7] != '}')
                return false;
            color = (color & 0xFF000000u) | rgb;
            return true;
        }

        void DrawTextRaw(const char* text, RECT rect, D3DCOLOR color, DWORD format, std::size_t count = static_cast<std::size_t>(-1), bool colorTags = false, int passwordSize = 0,
                         const RECT* clip = nullptr)
        {
            ZoneScoped;
            if (!g_device || !g_font.texture || !text)
                return;

            count = std::min(count, std::strlen(text));
            g_textVertices.reserve(count * 6);
            int textWidth = MeasureText(text, count, passwordSize);
            if (colorTags)
            {
                textWidth = 0;
                D3DCOLOR ignored = color;
                for (std::size_t i = 0; text[i] && i < count; ++i)
                {
                    if (count - i >= 8 && ReadColorTag(text + i, ignored))
                        i += 7;
                    else
                        textWidth += g_font.glyphs[static_cast<unsigned char>(text[i])].advance;
                }
            }
            int x = rect.left;
            int y = rect.top;
            if (format & DT_CENTER)
                x = rect.left + (Width(rect) - textWidth) / 2;
            else if (format & DT_RIGHT)
                x = rect.right - textWidth;
            if (format & DT_VCENTER)
                y = rect.top + (Height(rect) - (passwordSize ? passwordSize : g_font.height)) / 2;
            else if (format & DT_BOTTOM)
                y = rect.bottom - g_font.height;

            RECT clipped{};
            if (clip)
            {
                D3DVIEWPORT9 viewport{};
                viewport = g_passViewport;
                if (!g_passDepth && FAILED(g_device->GetViewport(&viewport)))
                    return;
                const RECT bounds{static_cast<LONG>(viewport.X), static_cast<LONG>(viewport.Y), static_cast<LONG>(viewport.X + viewport.Width),
                                  static_cast<LONG>(viewport.Y + viewport.Height)};
                if (!IntersectRect(&clipped, clip, &bounds))
                    return;
            }
            g_textVertices.clear();
            for (std::size_t i = 0; text[i] && i < count; ++i)
            {
                if (colorTags && count - i >= 8 && ReadColorTag(text + i, color))
                {
                    i += 7;
                    continue;
                }
                const FontGlyph& glyph = g_font.glyphs[static_cast<unsigned char>(text[i])];
                const D3DCOLOR fadedColor = ui::FadeColor(color);
                const RECT dst{x, y, x + (passwordSize ? passwordSize : glyph.width), y + (passwordSize ? passwordSize : glyph.height)};
                const float l = static_cast<float>(dst.left) - 0.5f;
                const float t = static_cast<float>(dst.top) - 0.5f;
                const float r = static_cast<float>(dst.right) - 0.5f;
                const float b = static_cast<float>(dst.bottom) - 0.5f;
                const Vertex vertices[6] = {
                    {l, t, 0.0f, 1.0f, fadedColor, glyph.u0, glyph.v0}, {r, t, 0.0f, 1.0f, fadedColor, glyph.u1, glyph.v0}, {l, b, 0.0f, 1.0f, fadedColor, glyph.u0, glyph.v1},
                    {l, b, 0.0f, 1.0f, fadedColor, glyph.u0, glyph.v1}, {r, t, 0.0f, 1.0f, fadedColor, glyph.u1, glyph.v0}, {r, b, 0.0f, 1.0f, fadedColor, glyph.u1, glyph.v1},
                };
                g_textVertices.insert(g_textVertices.end(), std::begin(vertices), std::end(vertices));
                x += passwordSize ? passwordSize + 2 : glyph.advance;
            }
            if (!g_textVertices.empty())
                SubmitUi(g_textVertices.data(), g_textVertices.size(), g_font.texture, clip ? &clipped : nullptr);
        }

        void DrawText(const char* text, const RECT& rect, D3DCOLOR color, DWORD format)
        {
            ZoneScoped;
            DrawTextRaw(text, rect, color, format, static_cast<std::size_t>(-1), true);
        }

        void SetupDefaultElements()
        {
            ZoneScoped;
            for (auto& elements : g_defaultElements)
                elements.clear();

            auto add = [](ControlType type, unsigned int element, int l, int t, int r, int b, D3DCOLOR textureColor = 0xFFFFFFFFu, D3DCOLOR fontColor = 0xFFFFFFFFu,
                          DWORD format = DT_CENTER | DT_VCENTER | DT_SINGLELINE) {
                auto& list = g_defaultElements[static_cast<std::size_t>(type)];
                if (list.size() <= element)
                    list.resize(element + 1);
                list[element].texture = RECT{l, t, r, b};
                list[element].textureColor = textureColor;
                list[element].fontColor = fontColor;
                list[element].textFormat = format;
                for (unsigned int state = 0; state < 6; ++state)
                {
                    list[element].textureStates[state] = StateColor(textureColor, static_cast<ControlState>(state));
                    list[element].fontStates[state] = state == 1 ? 0xC8808080u : state == 2 ? 0 : fontColor;
                }
            };

            // Exact RECTs recovered from the R5 default-element initializer at samp.dll+0x8D780.
            add(ControlType::Static, 0, 17, 269, 241, 287, 0xFFFFFFFFu, 0xFFD2D2D2u, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            add(ControlType::Button, 0, 0, 0, 136, 54, 0xC8FFFFFFu, 0xFFC8C8C8u);
            add(ControlType::Button, 1, 0, 0, 136, 54, 0x00FFFFFFu, 0xFFC8C8C8u);
            auto& buttons = g_defaultElements[static_cast<std::size_t>(ControlType::Button)];
            // button colors from r5's default elements at 0x8d780
            for (auto& button : buttons)
            {
                button.textureStates[4] = 0xFFB92228u;
                button.fontStates[3] = button.fontStates[4] = button.fontStates[5] = 0xFFFFFFFFu;
            }
            buttons[0].textureStates[3] = 0xFFFFFFFFu;
            buttons[0].textureStates[5] = 0xFF95B0D0u;
            buttons[1].textureStates[3] = 0xC8FFFFFFu;
            buttons[1].textureStates[5] = 0x3C505050u;
            add(ControlType::CheckBox, 0, 0, 54, 27, 81, 0x96FFFFFFu, 0xFFFFFFFFu, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            add(ControlType::CheckBox, 1, 27, 54, 54, 81, 0x96FFFFFFu, 0xFFFFFFFFu, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            add(ControlType::RadioButton, 0, 54, 54, 81, 81, 0x96FFFFFFu, 0xFFFFFFFFu, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            add(ControlType::RadioButton, 1, 81, 54, 108, 81, 0x96FFFFFFu, 0xFFFFFFFFu, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            add(ControlType::ComboBox, 0, 7, 81, 247, 123, 0xC8FFFFFFu, 0xFFFFFFFFu, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            add(ControlType::ComboBox, 1, 272, 0, 325, 49, 0xC8FFFFFFu, 0xFFFFFFFFu);
            add(ControlType::ComboBox, 2, 7, 123, 241, 265, 0xFFFFFFFFu, 0xFFC8C8C8u, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            add(ControlType::ComboBox, 3, 7, 266, 241, 289, 0xFFFFFFFFu, 0xFFC8C8C8u, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            add(ControlType::Slider, 0, 1, 290, 280, 331, 0x96FFFFFFu);
            add(ControlType::Slider, 1, 54, 54, 81, 81, 0xFFFFFFFFu);

            add(ControlType::ScrollBar, 0, 243, 144, 265, 155);
            add(ControlType::ScrollBar, 1, 243, 124, 265, 144);
            add(ControlType::ScrollBar, 2, 243, 155, 265, 176);
            add(ControlType::ScrollBar, 3, 266, 123, 286, 167, 0xFFB92228u);

            const RECT edit[9] = {
                {14, 90, 241, 113},  {8, 82, 14, 90},   {14, 82, 241, 90},   {241, 82, 246, 90},   {8, 90, 14, 113},
                {241, 90, 246, 113}, {8, 113, 14, 121}, {14, 113, 241, 121}, {241, 113, 246, 121},
            };
            for (unsigned int i = 0; i < 9; ++i)
            {
                add(ControlType::EditBox, i, edit[i].left, edit[i].top, edit[i].right, edit[i].bottom, 0xFFFFFFFFu, 0xFFFFFFFFu, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
                add(ControlType::ImeEditBox, i, edit[i].left, edit[i].top, edit[i].right, edit[i].bottom, 0xFFFFFFFFu, 0xFFFFFFFFu, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            }
            add(ControlType::ImeEditBox, 9, 0, 0, 136, 54);
            add(ControlType::ListBox, 0, 13, 124, 241, 265, 0xFFFFFFFFu, 0xFFFFFFFFu, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            add(ControlType::ListBox, 1, 17, 269, 241, 287, 0xFFFFFFFFu, 0xFFFFFFFFu, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }

        void SetupDialogs()
        {
            ZoneScoped;
            D3DVIEWPORT9 viewport{};
            if (FAILED(g_device->GetViewport(&viewport)))
            {
                viewport.Width = 640;
                viewport.Height = 480;
            }

            for (Dialog& dialog : g_dialogs)
                dialog.Reset();

            Dialog& scoreboard = g_dialogs[Index(DialogId::Scoreboard)];
            scoreboard.SetSize(640, 480);
            scoreboard.SetVisible(false);
            scoreboard.SetBackgroundColors(kScoreboardBackground);

            Dialog& gameUi = g_dialogs[Index(DialogId::GameUI)];
            gameUi.SetSize(static_cast<int>(viewport.Width), static_cast<int>(viewport.Height));
            gameUi.SetVisible(true);
            gameUi.SetBackgroundColors(0);

            Dialog& classSelection = g_dialogs[Index(DialogId::ClassSelection)];
            classSelection.SetSize(310, 40);
            classSelection.SetVisible(false);
            classSelection.EnableKeyboardInput(false);
            classSelection.SetBackgroundColors(kScoreboardBackground);
            // r5 class selection controls from the initializer at 0xc5902
            classSelection.AddButton(6, "<<", 10, 5, 90, 30);
            classSelection.AddButton(5, ">>", 110, 5, 90, 30);
            classSelection.AddButton(4, "Spawn", 210, 5, 90, 30);

            Dialog& dialog = g_dialogs[Index(DialogId::Dialog)];
            dialog.SetSize(600, 300);
            dialog.SetVisible(false);
            dialog.SetBackgroundColors(kDialogBackground);

            Dialog& secondary = g_dialogs[Index(DialogId::Secondary)];
            secondary.SetSize(500, 160);
            secondary.SetVisible(false);
            secondary.SetBackgroundColors(kDialogBackground);

            Dialog& auxiliary = g_dialogs[Index(DialogId::Auxiliary)];
            auxiliary.SetSize(500, 160);
            auxiliary.SetVisible(false);
            auxiliary.EnableKeyboardInput(false);
            auxiliary.SetBackgroundColors(kDialogBackground);
        }

    }

    Control::Control(ControlType type, int id, const char* text, int x, int y, int width, int height)
        : m_type(type), m_id(id), m_x(x), m_y(y), m_width(width), m_height(height), m_text(text ? text : "")
    {}

    void Control::SetLocation(int x, int y)
    {
        m_x = x;
        m_y = y;
    }
    void Control::SetSize(int width, int height)
    {
        m_width = width;
        m_height = height;
    }
    RECT Control::GetRect() const
    {
        return RECT{m_x, m_y, m_x + m_width, m_y + m_height};
    }
    void Control::SetText(const char* text)
    {
        m_text = text ? text : "";
    }
    RECT Control::GetScreenRect() const
    {
        ZoneScoped;
        RECT rect = GetRect();
        if (m_dialog)
        {
            POINT p{};
            m_dialog->GetLocation(p);
            OffsetRect(&rect, p.x, p.y);
        }
        return rect;
    }
    bool Control::Contains(int x, int y) const
    {
        return PtIn(GetScreenRect(), x, y);
    }
    void Control::OnFocus(bool focus)
    {
        m_focus = focus;
    }
    ControlState Control::GetState() const
    {
        ZoneScoped;
        if (!m_visible)
            return ControlState::Hidden;
        if (!m_enabled)
            return ControlState::Disabled;
        if (m_pressed)
            return ControlState::Pressed;
        if (m_mouseOver)
            return ControlState::MouseOver;
        if (m_focus)
            return ControlState::Focus;
        return ControlState::Normal;
    }
    void Control::SendEvent(unsigned int event)
    {
        ZoneScoped;
        if (m_dialog)
            m_dialog->SendEvent(event, this);
    }
    void Control::Render()
    {
        ZoneScoped;
    }
    bool Control::MsgProc(UINT, WPARAM, LPARAM)
    {
        return false;
    }

    Button::Button(int id, const char* text, int x, int y, int width, int height) : Control(ControlType::Button, id, text, x, y, width, height)
    {}
    void Button::Render()
    {
        ZoneScoped;
        if (!m_visible)
        {
            m_renderTick = GetTickCount64();
            return;
        }
        RECT rect = GetScreenRect();
        const ControlState state = GetState();
        // r5's button render at 0x8e550 offsets both layers and blends their colors
        if (state == ControlState::Pressed)
            OffsetRect(&rect, 1, 2);
        else if (state == ControlState::MouseOver)
            OffsetRect(&rect, -1, -2);

        const ULONGLONG now = GetTickCount64();
        const bool firstRender = m_renderTick == 0;
        const float elapsed = m_renderTick ? static_cast<float>(now - m_renderTick) / 1000.0f : 1.0f / 60.0f;
        m_renderTick = now;
        const float blend = firstRender || state == ControlState::Pressed ? 1.0f : 1.0f - std::pow(0.8f, 30.0f * elapsed);
        for (unsigned int layer = 0; layer < 2; ++layer)
        {
            const SkinElement* skin = GetDefaultElement(ControlType::Button, layer);
            if (!skin)
                continue;
            auto blendColor = [&](unsigned int kind, D3DCOLOR target) {
                D3DCOLOR color = 0;
                for (unsigned int channel = 0; channel < 4; ++channel)
                {
                    float& current = m_colors[layer][kind][channel];
                    current += (static_cast<float>((target >> (channel * 8)) & 0xFF) - current) * blend;
                    color |= static_cast<D3DCOLOR>(current + 0.5f) << (channel * 8);
                }
                return color;
            };
            const unsigned int index = static_cast<unsigned int>(state);
            const D3DCOLOR texture = blendColor(0, skin->textureStates[index]);
            const D3DCOLOR font = blendColor(1, skin->fontStates[index]);
            DrawQuad(rect, texture, g_skin, &skin->texture);
            DrawText(m_text.c_str(), rect, font, skin->textFormat);
        }
    }
    bool Button::MsgProc(UINT message, WPARAM wParam, LPARAM lParam)
    {
        ZoneScoped;
        if (!m_visible || !m_enabled)
            return false;
        const int x = GET_X_LPARAM(lParam), y = GET_Y_LPARAM(lParam);
        if (message == WM_LBUTTONDOWN && Contains(x, y))
        {
            m_pressed = true;
            return true;
        }
        if (message == WM_LBUTTONUP)
        {
            const bool fire = m_pressed && Contains(x, y);
            m_pressed = false;
            if (fire)
            {
                SendEvent(EVENT_BUTTON_CLICKED);
                return true;
            }
        }
        if (message == WM_KEYDOWN && m_focus && (wParam == VK_RETURN || wParam == VK_SPACE))
        {
            m_pressed = true;
            return true;
        }
        if (message == WM_KEYUP && m_focus && (wParam == VK_RETURN || wParam == VK_SPACE))
        {
            m_pressed = false;
            SendEvent(EVENT_BUTTON_CLICKED);
            return true;
        }
        return false;
    }

    Static::Static(int id, const char* text, int x, int y, int width, int height) : Control(ControlType::Static, id, text, x, y, width, height)
    {}
    void Static::Render()
    {
        ZoneScoped;
        if (!m_visible)
            return;
        const ControlState state = GetState();
        DrawText(m_text.c_str(), GetScreenRect(), ElementFontColor(ControlType::Static, 0, state), ElementTextFormat(ControlType::Static, 0, DT_LEFT | DT_VCENTER | DT_SINGLELINE));
    }

    CheckBox::CheckBox(int id, const char* text, int x, int y, int width, int height, bool checked)
        : Control(ControlType::CheckBox, id, text, x, y, width, height), m_checked(checked)
    {}
    void CheckBox::Render()
    {
        ZoneScoped;
        if (!m_visible)
            return;
        RECT rect = GetScreenRect();
        const int side = std::min(27, Height(rect));
        RECT box{rect.left, rect.top + (Height(rect) - side) / 2, rect.left + side, rect.top + (Height(rect) + side) / 2};
        const unsigned int element = m_checked ? 1u : 0u;
        const ControlState state = GetState();
        DrawSkin(ControlType::CheckBox, element, box, state);
        rect.left += side + 4;
        DrawText(m_text.c_str(), rect, ElementFontColor(ControlType::CheckBox, element, state),
                 ElementTextFormat(ControlType::CheckBox, element, DT_LEFT | DT_VCENTER | DT_SINGLELINE));
    }
    bool CheckBox::MsgProc(UINT message, WPARAM, LPARAM lParam)
    {
        ZoneScoped;
        if (!m_visible || !m_enabled)
            return false;
        const int x = GET_X_LPARAM(lParam), y = GET_Y_LPARAM(lParam);
        if (message == WM_LBUTTONDOWN && Contains(x, y))
        {
            m_pressed = true;
            return true;
        }
        if (message == WM_LBUTTONUP)
        {
            const bool fire = m_pressed && Contains(x, y);
            m_pressed = false;
            if (fire)
            {
                m_checked = !m_checked;
                SendEvent(EVENT_CHECKBOX_CHANGED);
                return true;
            }
        }
        return false;
    }

    RadioButton::RadioButton(int id, unsigned int group, const char* text, int x, int y, int width, int height, bool checked)
        : Control(ControlType::RadioButton, id, text, x, y, width, height), m_group(group), m_checked(checked)
    {}
    void RadioButton::Render()
    {
        ZoneScoped;
        if (!m_visible)
            return;
        RECT rect = GetScreenRect();
        const int side = std::min(27, Height(rect));
        RECT box{rect.left, rect.top + (Height(rect) - side) / 2, rect.left + side, rect.top + (Height(rect) + side) / 2};
        const unsigned int element = m_checked ? 1u : 0u;
        const ControlState state = GetState();
        DrawSkin(ControlType::RadioButton, element, box, state);
        rect.left += side + 4;
        DrawText(m_text.c_str(), rect, ElementFontColor(ControlType::RadioButton, element, state),
                 ElementTextFormat(ControlType::RadioButton, element, DT_LEFT | DT_VCENTER | DT_SINGLELINE));
    }
    bool RadioButton::MsgProc(UINT message, WPARAM, LPARAM lParam)
    {
        ZoneScoped;
        if (!m_visible || !m_enabled)
            return false;
        const int x = GET_X_LPARAM(lParam), y = GET_Y_LPARAM(lParam);
        if (message == WM_LBUTTONDOWN && Contains(x, y))
        {
            m_pressed = true;
            return true;
        }
        if (message == WM_LBUTTONUP)
        {
            const bool fire = m_pressed && Contains(x, y);
            m_pressed = false;
            if (fire && !m_checked)
            {
                if (m_dialog)
                    m_dialog->ClearRadioButtonGroup(m_group, this);
                m_checked = true;
                SendEvent(EVENT_RADIOBUTTON_CHANGED);
                return true;
            }
        }
        return false;
    }

    ComboBox::ComboBox(int id, int x, int y, int width, int height) : Control(ControlType::ComboBox, id, "", x, y, width, height)
    {}
    void ComboBox::AddItem(const char* text, void* data)
    {
        m_items.push_back({text ? text : "", data});
        if (m_selected < 0)
            m_selected = 0;
    }
    void ComboBox::RemoveAllItems()
    {
        m_items.clear();
        m_selected = -1;
    }
    const char* ComboBox::GetSelectedText() const
    {
        return m_selected >= 0 && m_selected < static_cast<int>(m_items.size()) ? m_items[m_selected].text.c_str() : "";
    }
    void ComboBox::SetSelectedIndex(int index)
    {
        if (index < 0 || index >= static_cast<int>(m_items.size()))
            return;
        m_selected = index;
        SendEvent(EVENT_COMBOBOX_SELECTION_CHANGED);
    }
    void ComboBox::Render()
    {
        ZoneScoped;
        if (!m_visible)
            return;
        RECT rect = GetScreenRect();
        DrawSkin(ControlType::ComboBox, 0, rect, GetState());
        RECT button{rect.right - m_height, rect.top, rect.right, rect.bottom};
        DrawSkin(ControlType::ComboBox, 1, button, GetState());
        RECT textRect = rect;
        textRect.left += 6;
        textRect.right = button.left - 2;
        DrawText(GetSelectedText(), textRect, ElementFontColor(ControlType::ComboBox, 0, GetState()),
                 ElementTextFormat(ControlType::ComboBox, 0, DT_LEFT | DT_VCENTER | DT_SINGLELINE));
        if (m_open && !m_items.empty())
        {
            const int rowH = std::max(18, m_height);
            RECT list{rect.left, rect.bottom, rect.right, rect.bottom + rowH * static_cast<int>(m_items.size())};
            DrawSkin(ControlType::ComboBox, 2, list, ControlState::Normal);
            for (std::size_t i = 0; i < m_items.size(); ++i)
            {
                RECT row{list.left + 4, list.top + static_cast<int>(i) * rowH, list.right - 4, list.top + static_cast<int>(i + 1) * rowH};
                if (static_cast<int>(i) == m_selected)
                    DrawQuad(row, 0x60305070u);
                DrawText(m_items[i].text.c_str(), row, kTextColor, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            }
        }
    }
    bool ComboBox::MsgProc(UINT message, WPARAM, LPARAM lParam)
    {
        ZoneScoped;
        if (!m_visible || !m_enabled)
            return false;
        const int x = GET_X_LPARAM(lParam), y = GET_Y_LPARAM(lParam);
        RECT rect = GetScreenRect();
        if (message == WM_LBUTTONUP && PtIn(rect, x, y))
        {
            m_open = !m_open;
            return true;
        }
        if (message == WM_LBUTTONUP && m_open)
        {
            const int rowH = std::max(18, m_height);
            RECT list{rect.left, rect.bottom, rect.right, rect.bottom + rowH * static_cast<int>(m_items.size())};
            if (PtIn(list, x, y))
            {
                const int index = (y - list.top) / rowH;
                m_open = false;
                SetSelectedIndex(index);
                return true;
            }
            m_open = false;
        }
        return false;
    }

    Slider::Slider(int id, int x, int y, int width, int height, int minValue, int maxValue, int value) : Control(ControlType::Slider, id, "", x, y, width, height)
    {
        SetRange(minValue, maxValue);
        SetValue(value);
    }
    void Slider::SetRange(int minValue, int maxValue)
    {
        if (minValue > maxValue)
            std::swap(minValue, maxValue);
        m_min = minValue;
        m_max = maxValue;
        SetValue(m_value);
    }
    void Slider::SetValue(int value)
    {
        m_value = std::clamp(value, m_min, m_max);
    }
    void Slider::SetValueFromPoint(int x)
    {
        ZoneScoped;
        RECT rect = GetScreenRect();
        const float t = std::clamp(static_cast<float>(x - rect.left) / std::max(1, Width(rect)), 0.0f, 1.0f);
        const int next = m_min + static_cast<int>(std::lround((m_max - m_min) * t));
        if (next != m_value)
        {
            m_value = next;
            SendEvent(EVENT_SLIDER_VALUE_CHANGED);
        }
    }
    void Slider::Render()
    {
        ZoneScoped;
        if (!m_visible)
            return;
        RECT rect = GetScreenRect();
        DrawSkin(ControlType::Slider, 0, rect, GetState());
        const float t = m_max == m_min ? 0.0f : static_cast<float>(m_value - m_min) / (m_max - m_min);
        const int cx = rect.left + static_cast<int>(t * Width(rect));
        RECT thumb{cx - m_height / 2, rect.top, cx + m_height / 2, rect.bottom};
        DrawSkin(ControlType::Slider, 1, thumb, m_dragging ? ControlState::Pressed : GetState());
    }
    bool Slider::MsgProc(UINT message, WPARAM, LPARAM lParam)
    {
        ZoneScoped;
        if (!m_visible || !m_enabled)
            return false;
        const int x = GET_X_LPARAM(lParam), y = GET_Y_LPARAM(lParam);
        if (message == WM_LBUTTONDOWN && Contains(x, y))
        {
            m_dragging = true;
            m_pressed = true;
            SetValueFromPoint(x);
            return true;
        }
        if (message == WM_MOUSEMOVE && m_dragging)
        {
            SetValueFromPoint(x);
            return true;
        }
        if (message == WM_LBUTTONUP && m_dragging)
        {
            SetValueFromPoint(x);
            m_dragging = false;
            m_pressed = false;
            SendEvent(EVENT_SLIDER_VALUE_CHANGED_UP);
            return true;
        }
        return false;
    }

    EditBox::EditBox(int id, const char* text, int x, int y, int width, int height) : Control(ControlType::EditBox, id, "", x, y, width, height)
    {
        SetText(text);
    }
    EditBox::~EditBox()
    {
        ime::Blur(this);
        Release(m_textTexture);
    }
    void EditBox::OnFocus(bool focus)
    {
        Control::OnFocus(focus);
        if (focus)
            ime::Focus(this);
        else
            ime::Blur(this);
    }
    void EditBox::SetPassword(bool password)
    {
        if (m_password == password)
            return;
        m_password = password;
    }
    RECT EditBox::GetImeRect() const
    {
        UpdateScroll();
        const RECT content = GetImeTextRect();
        RECT rect = content;
        rect.left = std::min<LONG>(content.right, content.left + TextWidth(m_caret) - TextWidth(m_firstVisible));
        return rect;
    }
    int EditBox::TextWidth(std::size_t bytes) const
    {
        ZoneScoped;
        const int passwordSize = m_password ? std::max(1, Height(GetScreenRect()) / 3) : 0;
        if (m_widthText != m_buffer || m_widthRevision != g_fontRevision || m_widthPasswordSize != passwordSize || m_prefixWidths.empty())
        {
            m_widthText = m_buffer;
            m_widthRevision = g_fontRevision;
            m_widthPasswordSize = passwordSize;
            m_prefixWidths.assign(m_buffer.size() + 1, 0);
            bool measured = false;
            const bool multibyte = !m_password && HasMultibyteText(m_buffer.c_str());
            if (multibyte)
            {
                SIZE extent{};
                if (HDC dc = MeasurementDc())
                    measured = GetTextExtentExPointA(dc, m_buffer.data(), static_cast<int>(m_buffer.size()), 0, nullptr, m_prefixWidths.data() + 1, &extent) != FALSE;
            }
            if (!measured)
            {
                for (std::size_t first = 0; first < m_buffer.size();)
                {
                    const std::size_t next = NextCharacter(m_buffer.c_str(), first);
                    const int width = m_password  ? m_prefixWidths[first] + passwordSize + 2
                                      : multibyte ? MeasureText(m_buffer.c_str(), next)
                                                  : m_prefixWidths[first] + g_font.glyphs[static_cast<unsigned char>(m_buffer[first])].advance;
                    std::fill(m_prefixWidths.begin() + first + 1, m_prefixWidths.begin() + next + 1, width);
                    first = next;
                }
            }
            m_masked.assign(CharacterCount(m_buffer.c_str(), m_buffer.size()), kPasswordGlyph);
        }
        return m_prefixWidths[std::min(bytes, m_buffer.size())];
    }
    void EditBox::UpdateScroll() const
    {
        ZoneScoped;
        // r5 0x85470 and 0.2x PlaceCaret: scroll by whole characters to keep the caret visible
        m_firstVisible = CharacterBoundary(m_buffer.c_str(), std::min(m_firstVisible, m_buffer.size()));
        if (m_caret < m_firstVisible)
            m_firstVisible = m_caret;
        const RECT content = GetImeTextRect();
        const int width = std::max(0, Width(content) - 2);
        const std::size_t trailing = m_caret < m_buffer.size() ? NextCharacter(m_buffer.c_str(), m_caret) : m_caret;
        const int right = TextWidth(trailing);
        while (m_firstVisible < m_caret && right - TextWidth(m_firstVisible) > width)
            m_firstVisible = NextCharacter(m_buffer.c_str(), m_firstVisible);
    }
    RECT EditBox::GetImeTextRect() const
    {
        const RECT rect = GetScreenRect();
        return RECT{rect.left + 7, rect.top + 2, std::max<LONG>(rect.left + 7, rect.right - 6 - ime::GetIndicatorWidth(this)), rect.bottom - 2};
    }
    bool EditBox::InsertText(const wchar_t* text)
    {
        ZoneScoped;
        if (!text || !*text)
            return false;
        BOOL substituted = FALSE;
        const UINT codePage = GetACP();
        const DWORD flags = codePage == CP_UTF8 ? WC_ERR_INVALID_CHARS : WC_NO_BEST_FIT_CHARS;
        BOOL* usedDefault = codePage == CP_UTF8 ? nullptr : &substituted;
        const int bytes = WideCharToMultiByte(codePage, flags, text, -1, nullptr, 0, nullptr, usedDefault);
        if (bytes <= 1 || substituted)
            return false;
        std::string encoded(static_cast<std::size_t>(bytes), '\0');
        if (!WideCharToMultiByte(codePage, flags, text, -1, encoded.data(), bytes, nullptr, usedDefault) || substituted)
            return false;
        encoded.pop_back();
        // reject the whole commit rather than splitting a multibyte character
        const std::size_t selected = std::max(m_caret, m_selectionAnchor) - std::min(m_caret, m_selectionAnchor);
        if (encoded.size() > m_maxLength - (m_buffer.size() - selected))
            return false;
        DeleteSelection();
        m_buffer.insert(m_caret, encoded);
        m_caret += encoded.size();
        m_selectionAnchor = m_caret;
        SendEvent(EVENT_EDITBOX_CHANGE);
        return true;
    }
    void EditBox::SetText(const char* text)
    {
        m_buffer = text ? text : "";
        m_firstVisible = 0;
        if (m_buffer.size() > m_maxLength)
            m_buffer.resize(CharacterBoundary(m_buffer.c_str(), m_maxLength));
        m_caret = m_buffer.size();
        m_selectionAnchor = m_caret;
    }
    void EditBox::SetMaxLength(std::size_t length)
    {
        m_maxLength = length;
        if (m_buffer.size() > length)
            m_buffer.resize(CharacterBoundary(m_buffer.c_str(), length));
        m_caret = CharacterBoundary(m_buffer.c_str(), std::min(m_caret, m_buffer.size()));
        m_selectionAnchor = CharacterBoundary(m_buffer.c_str(), std::min(m_selectionAnchor, m_buffer.size()));
    }
    void EditBox::SetCaret(std::size_t caret)
    {
        m_caret = CharacterBoundary(m_buffer.c_str(), std::min(caret, m_buffer.size()));
        m_selectionAnchor = m_caret;
    }
    void EditBox::SelectAll()
    {
        m_selectionAnchor = 0;
        m_caret = m_buffer.size();
    }
    void EditBox::ClearSelection()
    {
        m_selectionAnchor = m_caret;
    }
    bool EditBox::CopySelection()
    {
        ZoneScoped;
        if (!HasSelection() || m_password)
            return false;
        const std::size_t first = std::min(m_caret, m_selectionAnchor);
        const int length = static_cast<int>(std::max(m_caret, m_selectionAnchor) - first);
        const int chars = MultiByteToWideChar(GetACP(), MB_ERR_INVALID_CHARS, m_buffer.data() + first, length, nullptr, 0);
        if (chars <= 0)
            return false;
        HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, (static_cast<std::size_t>(chars) + 1) * sizeof(wchar_t));
        if (!memory)
            return false;
        auto* output = static_cast<wchar_t*>(GlobalLock(memory));
        if (!output)
        {
            GlobalFree(memory);
            return false;
        }
        const bool converted = MultiByteToWideChar(GetACP(), MB_ERR_INVALID_CHARS, m_buffer.data() + first, length, output, chars) != 0;
        output[chars] = L'\0';
        GlobalUnlock(memory);
        if (!converted || !OpenClipboard(GetActiveWindow()))
        {
            GlobalFree(memory);
            return false;
        }
        const bool copied = EmptyClipboard() && SetClipboardData(CF_UNICODETEXT, memory);
        CloseClipboard();
        if (!copied)
            GlobalFree(memory);
        return copied;
    }

    void EditBox::PasteText()
    {
        ZoneScoped;
        if (!OpenClipboard(GetActiveWindow()))
            return;
        HGLOBAL memory = GetClipboardData(CF_UNICODETEXT);
        auto* input = memory ? static_cast<const wchar_t*>(GlobalLock(memory)) : nullptr;
        std::wstring text;
        if (input)
        {
            const std::size_t limit = std::min<std::size_t>(GlobalSize(memory) / sizeof(wchar_t), m_maxLength + 1);
            for (std::size_t i = 0; i < limit && input[i]; ++i)
            {
                // edit boxes are single-line; clipboard newlines never submit a command
                if (input[i] == L'\r' || input[i] == L'\n' || input[i] == L'\t')
                    text += L' ';
                else if (input[i] >= L' ')
                    text += input[i];
            }
            GlobalUnlock(memory);
        }
        CloseClipboard();
        if (text.empty())
            return;
        // fit the selection's free space without splitting encoded characters
        const std::size_t selected = std::max(m_caret, m_selectionAnchor) - std::min(m_caret, m_selectionAnchor);
        const std::size_t available = m_maxLength - (m_buffer.size() - selected);
        std::size_t fit = 0;
        while (fit < text.size())
        {
            std::size_t next = fit + 1;
            if (text[fit] >= 0xD800 && text[fit] <= 0xDBFF && next < text.size() && text[next] >= 0xDC00 && text[next] <= 0xDFFF)
                ++next;
            const int bytes = WideCharToMultiByte(GetACP(), 0, text.data(), static_cast<int>(next), nullptr, 0, nullptr, nullptr);
            if (bytes <= 0 || static_cast<std::size_t>(bytes) > available)
                break;
            fit = next;
        }
        text.resize(fit);
        InsertText(text.c_str());
    }

    void EditBox::DeleteSelection()
    {
        if (!HasSelection())
            return;
        const std::size_t first = std::min(m_selectionAnchor, m_caret), last = std::max(m_selectionAnchor, m_caret);
        m_buffer.erase(first, last - first);
        m_caret = first;
        m_selectionAnchor = first;
    }
    void EditBox::InsertChar(char value)
    {
        ZoneScoped;
        DeleteSelection();
        if (m_buffer.size() >= m_maxLength)
            return;
        m_buffer.insert(m_buffer.begin() + static_cast<std::ptrdiff_t>(m_caret), value);
        ++m_caret;
        m_selectionAnchor = m_caret;
        SendEvent(EVENT_EDITBOX_CHANGE);
    }
    void EditBox::MoveCaret(int delta, bool selecting)
    {
        ZoneScoped;
        if (!selecting && HasSelection())
        {
            m_caret = delta < 0 ? std::min(m_caret, m_selectionAnchor) : std::max(m_caret, m_selectionAnchor);
            m_selectionAnchor = m_caret;
            return;
        }
        if (delta < 0 && m_caret)
            m_caret = CharacterBoundary(m_buffer.c_str(), m_caret - 1);
        else if (delta > 0 && m_caret < m_buffer.size())
            m_caret = NextCharacter(m_buffer.c_str(), m_caret);
        if (!selecting)
            m_selectionAnchor = m_caret;
    }
    std::size_t EditBox::WordBoundary(int direction) const
    {
        ZoneScoped;
        auto classify = [&](std::size_t index) {
            const auto ch = static_cast<unsigned char>(m_buffer[index]);
            return std::isspace(ch) ? 0 : (ch >= 0x80 || std::isalnum(ch) || ch == '_') ? 1 : 2;
        };
        std::size_t position = m_caret;
        if (direction < 0)
        {
            while (position && classify(CharacterBoundary(m_buffer.c_str(), position - 1)) == 0)
                position = CharacterBoundary(m_buffer.c_str(), position - 1);
            if (position)
            {
                const int type = classify(CharacterBoundary(m_buffer.c_str(), position - 1));
                do
                    position = CharacterBoundary(m_buffer.c_str(), position - 1);
                while (position && classify(CharacterBoundary(m_buffer.c_str(), position - 1)) == type);
            }
        }
        else
        {
            if (position < m_buffer.size())
            {
                const int type = classify(position);
                do
                    position = NextCharacter(m_buffer.c_str(), position);
                while (position < m_buffer.size() && classify(position) == type);
            }
            while (position < m_buffer.size() && classify(position) == 0)
                position = NextCharacter(m_buffer.c_str(), position);
        }
        return position;
    }

    void EditBox::Render()
    {
        ZoneScoped;
        if (!m_visible)
            return;
        RECT rect = GetScreenRect();
        rect.right -= ime::GetIndicatorWidth(this);
        DrawNineSlice(ControlType::EditBox, rect, GetState());
        RECT content = GetImeTextRect();
        UpdateScroll();
        const int scroll = TextWidth(m_firstVisible);
        const int passwordSize = m_password ? std::max(1, Height(rect) / 3) : 0;
        const char* display = m_password ? m_masked.c_str() + scroll / (passwordSize + 2) : m_buffer.c_str() + m_firstVisible;
        const std::size_t first = std::min(m_selectionAnchor, m_caret), last = std::max(m_selectionAnchor, m_caret);
        if (HasSelection())
        {
            const int x0 = content.left + TextWidth(first) - scroll;
            const int x1 = content.left + TextWidth(last) - scroll;
            RECT selection{x0, content.top + 2, x1, content.bottom - 2};
            RECT clipped{};
            if (IntersectRect(&clipped, &selection, &content))
                DrawQuad(clipped, m_selectionColor);
        }
        if (!m_password && HasMultibyteText(m_buffer.c_str()) && g_device)
        {
            if ((m_cachedText != m_buffer || m_cachedFace != g_fontFace || m_cachedHeight != g_fontHeight || m_cachedWeight != g_fontWeight || !m_textTexture) &&
                CreateTextTexture(m_buffer.c_str(), &m_textTexture, m_textExtent))
            {
                m_cachedText = m_buffer;
                m_cachedFace = g_fontFace;
                m_cachedHeight = g_fontHeight;
                m_cachedWeight = g_fontWeight;
            }
            if (m_textTexture && m_cachedText == m_buffer)
            {
                const LONG width = std::min<LONG>(std::max<LONG>(0, m_textExtent.cx - scroll), Width(content));
                const LONG height = std::min<LONG>(m_textExtent.cy, Height(content));
                const LONG top = content.top + (Height(content) - height) / 2;
                const RECT source{scroll, 0, scroll + width, height};
                DrawQuad(RECT{content.left, top, content.left + width, top + height}, m_enabled ? m_textColor : kDisabledTextColor, m_textTexture, &source);
            }
        }
        else
            DrawTextRaw(display, content, m_enabled ? m_textColor : kDisabledTextColor, DT_LEFT | DT_VCENTER | DT_SINGLELINE, static_cast<std::size_t>(-1), false, passwordSize,
                        &content);
        if (m_focus && m_enabled && !ime::OwnsComposition(this) && ((GetTickCount() / 500) & 1) == 0)
        {
            const int caretX = content.left + TextWidth(m_caret) - scroll;
            RECT caret{caretX - 1, content.top + 3, caretX + 1, content.bottom - 3};
            RECT clipped{};
            if (IntersectRect(&clipped, &caret, &content))
                DrawQuad(clipped, m_caretColor);
        }
    }
    bool EditBox::MsgProc(UINT message, WPARAM wParam, LPARAM lParam)
    {
        ZoneScoped;
        if (!m_visible || !m_enabled)
            return false;
        if (message == WM_LBUTTONDOWN && Contains(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)))
        {
            UpdateScroll();
            const RECT content = GetImeTextRect();
            const int px = GET_X_LPARAM(lParam) - content.left;
            std::size_t best = m_firstVisible;
            int current = 0;
            while (best < m_buffer.size())
            {
                const std::size_t next = NextCharacter(m_buffer.c_str(), best);
                const int adv = TextWidth(next) - TextWidth(best);
                if (px < current + adv / 2)
                    break;
                current += adv;
                best = next;
            }
            m_caret = best;
            m_selectionAnchor = best;
            return true;
        }
        if (!m_focus)
            return false;
        if (message == WM_COPY || message == WM_CUT)
        {
            if (CopySelection() && message == WM_CUT)
            {
                DeleteSelection();
                SendEvent(EVENT_EDITBOX_CHANGE);
            }
            return true;
        }
        if (message == WM_PASTE)
        {
            PasteText();
            return true;
        }
        if (message == WM_CHAR)
        {
            if (wParam == 1)
            {
                SelectAll();
                return true;
            }
            // ctrl-backspace is processed on keydown; windows emits DEL afterwards
            if (wParam == 3 || wParam == 22 || wParam == 24 || wParam == 127)
                return true;
            if (wParam == '\r')
            {
                SendEvent(EVENT_EDITBOX_STRING);
                return true;
            }
            if (wParam == '\b')
            {
                if ((GetKeyState(VK_CONTROL) | GetAsyncKeyState(VK_CONTROL)) & 0x8000)
                    return true;
                if (HasSelection())
                    DeleteSelection();
                else if (m_caret)
                {
                    const std::size_t previous = CharacterBoundary(m_buffer.c_str(), m_caret - 1);
                    m_buffer.erase(previous, m_caret - previous);
                    m_caret = previous;
                    m_selectionAnchor = m_caret;
                }
                SendEvent(EVENT_EDITBOX_CHANGE);
                return true;
            }
            if (wParam >= 0x20 && wParam <= 0xFF)
            {
                InsertChar(static_cast<char>(wParam));
                return true;
            }
        }
        if (message == WM_KEYDOWN)
        {
            const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            const bool ctrl = ((GetKeyState(VK_CONTROL) | GetAsyncKeyState(VK_CONTROL)) & 0x8000) != 0;
            if (ctrl && (wParam == 'C' || wParam == 'X' || wParam == VK_INSERT))
            {
                if (CopySelection() && wParam == 'X')
                {
                    DeleteSelection();
                    SendEvent(EVENT_EDITBOX_CHANGE);
                }
                return true;
            }
            if ((ctrl && wParam == 'V') || (shift && wParam == VK_INSERT))
            {
                PasteText();
                return true;
            }
            if (shift && wParam == VK_DELETE)
            {
                if (CopySelection())
                {
                    DeleteSelection();
                    SendEvent(EVENT_EDITBOX_CHANGE);
                }
                return true;
            }
            if (ctrl && wParam == VK_BACK)
            {
                if (HasSelection())
                    DeleteSelection();
                else
                {
                    const std::size_t previous = WordBoundary(-1);
                    m_buffer.erase(previous, m_caret - previous);
                    m_caret = m_selectionAnchor = previous;
                }
                SendEvent(EVENT_EDITBOX_CHANGE);
                return true;
            }
            if (wParam == VK_LEFT)
            {
                if (ctrl)
                {
                    m_caret = WordBoundary(-1);
                    if (!shift)
                        m_selectionAnchor = m_caret;
                }
                else
                    MoveCaret(-1, shift);
                return true;
            }
            if (wParam == VK_RIGHT)
            {
                if (ctrl)
                {
                    m_caret = WordBoundary(1);
                    if (!shift)
                        m_selectionAnchor = m_caret;
                }
                else
                    MoveCaret(1, shift);
                return true;
            }
            if (wParam == VK_HOME)
            {
                m_caret = 0;
                if (!shift)
                    m_selectionAnchor = m_caret;
                return true;
            }
            if (wParam == VK_END)
            {
                m_caret = m_buffer.size();
                if (!shift)
                    m_selectionAnchor = m_caret;
                return true;
            }
            if (wParam == VK_DELETE)
            {
                if (HasSelection())
                    DeleteSelection();
                else if (m_caret < m_buffer.size())
                    m_buffer.erase(m_caret, (ctrl ? WordBoundary(1) : NextCharacter(m_buffer.c_str(), m_caret)) - m_caret);
                SendEvent(EVENT_EDITBOX_CHANGE);
                return true;
            }
            if (ctrl && (wParam == 'A' || wParam == 'a'))
            {
                SelectAll();
                return true;
            }
        }
        return false;
    }

    ListBox::ListBox(int id, int x, int y, int width, int height) : Control(ControlType::ListBox, id, "", x, y, width, height)
    {}
    void ListBox::AddItem(const char* text, void* data)
    {
        Item item;
        item.text = text ? text : "";
        item.data = data;
        m_items.emplace_back(std::move(item));
        if (m_selected < 0)
            m_selected = 0;
    }
    void ListBox::SetItemText(int index, unsigned int column, const char* text)
    {
        ZoneScoped;
        if (index < 0 || index >= static_cast<int>(m_items.size()))
            return;

        Item& item = m_items[static_cast<std::size_t>(index)];
        if (column == 0)
        {
            item.text = text ? text : "";
            return;
        }

        const std::size_t subColumn = static_cast<std::size_t>(column - 1);
        if (item.columns.size() <= subColumn)
            item.columns.resize(subColumn + 1);
        item.columns[subColumn] = text ? text : "";
    }
    void ListBox::SetColumnWidth(unsigned int column, int width)
    {
        if (m_columnWidths.size() <= column)
            m_columnWidths.resize(static_cast<std::size_t>(column) + 1, 0);
        m_columnWidths[column] = std::max(0, width);
    }
    void ListBox::SetTextPadding(int padding)
    {
        m_textPadding = std::max(0, padding);
    }
    void ListBox::RemoveAllItems()
    {
        m_items.clear();
        m_selected = -1;
        m_top = 0;
        m_lastClickTick = 0;
        m_lastClickIndex = -1;
    }
    void ListBox::SetTopIndex(int index)
    {
        const int visible = GetVisibleRowCount();
        m_top = std::clamp(index, 0, std::max(0, static_cast<int>(m_items.size()) - visible));
    }
    void ListBox::RemoveItem(int index)
    {
        ZoneScoped;
        if (index < 0 || index >= static_cast<int>(m_items.size()))
            return;
        m_items.erase(m_items.begin() + index);
        if (m_selected == index)
            m_selected = -1;
        else if (m_selected > index)
            --m_selected;
        m_lastClickIndex = -1;
        m_lastClickTick = 0;
        SetTopIndex(m_top > index ? m_top - 1 : m_top);
    }
    void ListBox::SetSelectedIndex(int index)
    {
        ZoneScoped;
        if (index < 0 || index >= static_cast<int>(m_items.size()))
            return;
        const int visible = GetVisibleRowCount();
        if (index < m_top)
            m_top = index;
        else if (index >= m_top + visible)
            m_top = index - visible + 1;
        if (index != m_selected)
        {
            m_selected = index;
            SendEvent(EVENT_LISTBOX_SELECTION);
        }
    }
    int ListBox::GetVisibleRowCount() const
    {
        const int capacity = std::max(1, m_height / 18);
        return m_visibleRowCount > 0 ? std::min(capacity, m_visibleRowCount) : capacity;
    }
    void ListBox::SetVisibleRowCount(int count)
    {
        m_visibleRowCount = std::max(0, count);
        SetTopIndex(m_top);
    }
    const char* ListBox::GetSelectedText(unsigned int column) const
    {
        if (m_selected < 0 || m_selected >= static_cast<int>(m_items.size()))
            return "";
        const Item& item = m_items[m_selected];
        if (column == 0)
            return item.text.c_str();
        return column <= item.columns.size() ? item.columns[column - 1].c_str() : "";
    }
    void ListBox::Render()
    {
        ZoneScoped;
        if (!m_visible)
            return;
        RECT rect = GetScreenRect();
        DrawSkin(ControlType::ListBox, 0, rect, GetState());
        const int rowH = 18;
        const int visible = GetVisibleRowCount();
        for (int row = 0; row < visible; ++row)
        {
            const int index = m_top + row;
            if (index >= static_cast<int>(m_items.size()))
                break;
            RECT item{rect.left + m_textPadding, rect.top + row * rowH + 2, rect.right - m_textPadding, rect.top + (row + 1) * rowH + 2};
            if (index == m_selected)
            {
                RECT selection{rect.left, item.top, rect.right, item.bottom};
                DrawSkin(ControlType::ListBox, 1, selection, ControlState::Focus);
            }

            const Item& entry = m_items[static_cast<std::size_t>(index)];
            const D3DCOLOR textColor = ElementFontColor(ControlType::ListBox, 0, GetState());
            const DWORD textFormat = ElementTextFormat(ControlType::ListBox, 0, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            if (m_columnWidths.empty())
            {
                DrawText(entry.text.c_str(), item, textColor, textFormat);
                continue;
            }

            int left = item.left;
            const unsigned int columnCount = std::max<unsigned int>(1, static_cast<unsigned int>(entry.columns.size()) + 1);
            for (unsigned int column = 0; column < columnCount && left < item.right; ++column)
            {
                int right = item.right;
                if (column < m_columnWidths.size() && m_columnWidths[column] > 0)
                    right = std::min<int>(item.right, left + m_columnWidths[column]);

                RECT columnRect{left, item.top, right, item.bottom};
                const char* columnText = column == 0 ? entry.text.c_str() : entry.columns[static_cast<std::size_t>(column - 1)].c_str();
                DrawText(columnText, columnRect, textColor, textFormat);
                left = right;
            }
        }
    }
    bool ListBox::MsgProc(UINT message, WPARAM wParam, LPARAM lParam)
    {
        ZoneScoped;
        if (!m_visible || !m_enabled)
            return false;
        if (message == WM_KEYDOWN && m_focus && !m_items.empty())
        {
            int next = m_selected;
            switch (wParam)
            {
                case VK_UP:
                    --next;
                    break;
                case VK_DOWN:
                    ++next;
                    break;
                case VK_HOME:
                    next = 0;
                    break;
                case VK_END:
                    next = static_cast<int>(m_items.size()) - 1;
                    break;
                case VK_PRIOR:
                    next -= GetVisibleRowCount();
                    break;
                case VK_NEXT:
                    next += GetVisibleRowCount();
                    break;
                default:
                    return false;
            }
            SetSelectedIndex(std::clamp(next, 0, static_cast<int>(m_items.size()) - 1));
            return true;
        }
        RECT rect = GetScreenRect();
        const int x = GET_X_LPARAM(lParam), y = GET_Y_LPARAM(lParam);
        if (message == WM_LBUTTONUP && PtIn(rect, x, y))
        {
            const int row = (y - rect.top) / 18;
            if (row >= GetVisibleRowCount())
                return false;
            const int index = m_top + row;
            if (index >= 0 && index < static_cast<int>(m_items.size()))
            {
                SetSelectedIndex(index);
                const DWORD now = GetTickCount();
                if (m_lastClickIndex == index && now - m_lastClickTick <= GetDoubleClickTime())
                    SendEvent(EVENT_LISTBOX_ITEM_DBLCLK);
                m_lastClickIndex = index;
                m_lastClickTick = now;
                return true;
            }
        }
        if (message == WM_MOUSEWHEEL && PtIn(rect, x, y))
        {
            const short delta = GET_WHEEL_DELTA_WPARAM(wParam);
            m_top = std::clamp(m_top - (delta / WHEEL_DELTA) * 3, 0, std::max(0, static_cast<int>(m_items.size()) - 1));
            return true;
        }
        return false;
    }

    ScrollBar::ScrollBar(int id, int x, int y, int width, int height) : Control(ControlType::ScrollBar, id, "", x, y, width, height)
    {}
    int ScrollBar::ClampPosition(int value) const
    {
        return std::clamp(value, m_start, std::max(m_start, m_end - m_pageSize));
    }
    void ScrollBar::SetRange(int start, int end)
    {
        if (start > end)
            std::swap(start, end);
        m_start = start;
        m_end = end;
        m_position = ClampPosition(m_position);
    }
    void ScrollBar::SetPageSize(int pageSize)
    {
        m_pageSize = std::max(1, pageSize);
        m_position = ClampPosition(m_position);
    }
    void ScrollBar::SetTrackPos(int position)
    {
        m_position = ClampPosition(position);
    }
    void ScrollBar::Scroll(int delta)
    {
        SetTrackPos(m_position + delta);
    }
    RECT ScrollBar::GetUpRect() const
    {
        RECT r = GetScreenRect();
        return RECT{r.left, r.top, r.right, std::min(r.bottom, r.top + m_width)};
    }
    RECT ScrollBar::GetDownRect() const
    {
        RECT r = GetScreenRect();
        return RECT{r.left, std::max(r.top, r.bottom - m_width), r.right, r.bottom};
    }
    RECT ScrollBar::GetTrackRect() const
    {
        const RECT up = GetUpRect();
        const RECT down = GetDownRect();
        return RECT{up.left, up.bottom, down.right, std::max(up.bottom, down.top)};
    }
    RECT ScrollBar::GetThumbRect() const
    {
        ZoneScoped;
        const RECT r = GetTrackRect();
        const int track = Height(r);
        const int total = std::max(1, m_end - m_start);
        if (total <= m_pageSize || track <= 0)
            return RECT{r.left, r.top, r.right, r.top};
        // R5 UpdateThumbRect (+0x85320) uses an 8-pixel minimum and integer division.
        const int thumbH = std::min(track, std::max(8, track * m_pageSize / total));
        const int maxPos = std::max(m_start, m_end - m_pageSize);
        const int y = r.top + (m_position - m_start) * (track - thumbH) / (maxPos - m_start);
        return RECT{r.left, y, r.right, y + thumbH};
    }
    void ScrollBar::SetFromPoint(int y)
    {
        ZoneScoped;
        const RECT r = GetTrackRect();
        RECT thumb = GetThumbRect();
        const int thumbH = Height(thumb);
        const int track = Height(r) - thumbH;
        const int maxPos = std::max(m_start, m_end - m_pageSize);
        if (track <= 0 || maxPos == m_start)
            return;
        const int py = std::clamp<int>(y - m_dragOffset - r.top, 0, track);
        // R5 HandleMouse (+0x887D0) shifts by half a row before integer division.
        const int value = m_start + (py + track / ((maxPos - m_start) * 2)) * (maxPos - m_start) / track;
        SetTrackPos(value);
    }
    void ScrollBar::RepeatArrow()
    {
        ZoneScoped;
        if (!m_arrowDirection)
            return;
        const RECT arrow = m_arrowDirection < 0 ? GetUpRect() : GetDownRect();
        const ULONGLONG now = GetTickCount64();
        // R5 Render (+0x88A50): 0.33 seconds before repeating, then 0.05 seconds.
        if (PtIn(arrow, m_lastMouse.x, m_lastMouse.y) && now - m_arrowTick > (m_arrowRepeating ? 50u : 330u))
        {
            Scroll(m_arrowDirection);
            m_arrowRepeating = true;
            m_arrowTick = now;
        }
    }
    void ScrollBar::Render()
    {
        ZoneScoped;
        if (!m_visible)
        {
            m_arrowDirection = 0;
            return;
        }
        if (!m_enabled)
            m_arrowDirection = 0;
        RepeatArrow();
        const bool canScroll = m_end - m_start > m_pageSize;
        const ControlState state = m_enabled && canScroll ? ControlState::Normal : ControlState::Disabled;
        // R5 Render maps elements 0/1/2/3 to track/up/down/thumb respectively.
        DrawSkin(ControlType::ScrollBar, 0, GetTrackRect(), ControlState::Normal);
        DrawSkin(ControlType::ScrollBar, 1, GetUpRect(), ControlState::Normal);
        DrawSkin(ControlType::ScrollBar, 2, GetDownRect(), ControlState::Normal);
        if (const auto* thumb = GetDefaultElement(ControlType::ScrollBar, 3))
            DrawQuad(GetThumbRect(), state == ControlState::Disabled ? 0xFF95B0D0u : thumb->textureColor, g_skin, &thumb->texture);
    }
    bool ScrollBar::MsgProc(UINT message, WPARAM wParam, LPARAM lParam)
    {
        ZoneScoped;
        if (message == WM_CAPTURECHANGED || message == WM_KILLFOCUS)
        {
            m_dragging = false;
            m_arrowDirection = 0;
            return false;
        }
        if (!m_visible || !m_enabled)
            return false;
        const int x = GET_X_LPARAM(lParam), y = GET_Y_LPARAM(lParam);
        m_lastMouse = POINT{x, y};
        RECT all = GetScreenRect();
        if (message == WM_MOUSEWHEEL && PtIn(all, x, y))
        {
            Scroll(-(GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA) * 3);
            return true;
        }
        if ((message == WM_LBUTTONDOWN || message == WM_LBUTTONDBLCLK) && PtIn(all, x, y))
        {
            if (PtIn(GetUpRect(), x, y))
            {
                Scroll(-1);
                m_arrowDirection = -1;
            }
            else if (PtIn(GetDownRect(), x, y))
            {
                Scroll(1);
                m_arrowDirection = 1;
            }
            else if (PtIn(GetThumbRect(), x, y))
            {
                m_dragging = true;
                m_dragOffset = y - GetThumbRect().top;
            }
            else
            {
                const RECT thumb = GetThumbRect();
                Scroll(y < thumb.top ? -(m_pageSize - 1) : m_pageSize - 1);
            }
            m_arrowTick = GetTickCount64();
            m_arrowRepeating = false;
            return true;
        }
        if (message == WM_MOUSEMOVE && m_dragging)
        {
            SetFromPoint(y);
            return true;
        }
        if (message == WM_LBUTTONUP)
        {
            const bool handled = m_dragging || m_arrowDirection != 0;
            m_dragging = false;
            m_arrowDirection = 0;
            return handled;
        }
        return false;
    }

    void Dialog::Reset()
    {
        ZoneScoped;
        RemoveAllControls();
        m_x = m_y = m_width = m_height = 0;
        m_visible = true;
        m_mouseInput = true;
        m_appearance = {};
        m_keyboardInput = true;
        std::fill(std::begin(m_background), std::end(m_background), 0u);
        m_callback = nullptr;
        m_callbackContext = nullptr;
    }
    void Dialog::SetLocation(int x, int y)
    {
        m_x = x;
        m_y = y;
    }
    void Dialog::GetLocation(POINT& point) const
    {
        point.x = m_x;
        point.y = m_y;
    }
    void Dialog::SetSize(int width, int height)
    {
        m_width = width;
        m_height = height;
    }
    RECT Dialog::GetRect() const
    {
        return RECT{m_x, m_y, m_x + m_width, m_y + m_height};
    }
    void Dialog::SetBackgroundColors(D3DCOLOR color)
    {
        SetBackgroundColors(color, color, color, color);
    }
    void Dialog::SetBackgroundColors(D3DCOLOR tl, D3DCOLOR tr, D3DCOLOR bl, D3DCOLOR br)
    {
        m_background[0] = tl;
        m_background[1] = tr;
        m_background[2] = bl;
        m_background[3] = br;
    }
    void Dialog::SetCallback(EventCallback callback, void* context)
    {
        m_callback = callback;
        m_callbackContext = context;
    }
    Button* Dialog::AddButton(int id, const char* text, int x, int y, int width, int height)
    {
        return AddControl<Button>(id, text, x, y, width, height);
    }
    Static* Dialog::AddStatic(int id, const char* text, int x, int y, int width, int height)
    {
        return AddControl<Static>(id, text, x, y, width, height);
    }
    CheckBox* Dialog::AddCheckBox(int id, const char* text, int x, int y, int width, int height, bool checked)
    {
        return AddControl<CheckBox>(id, text, x, y, width, height, checked);
    }
    RadioButton* Dialog::AddRadioButton(int id, unsigned int group, const char* text, int x, int y, int width, int height, bool checked)
    {
        return AddControl<RadioButton>(id, group, text, x, y, width, height, checked);
    }
    ComboBox* Dialog::AddComboBox(int id, int x, int y, int width, int height)
    {
        return AddControl<ComboBox>(id, x, y, width, height);
    }
    Slider* Dialog::AddSlider(int id, int x, int y, int width, int height, int minValue, int maxValue, int value)
    {
        return AddControl<Slider>(id, x, y, width, height, minValue, maxValue, value);
    }
    EditBox* Dialog::AddEditBox(int id, const char* text, int x, int y, int width, int height)
    {
        return AddControl<EditBox>(id, text, x, y, width, height);
    }
    ListBox* Dialog::AddListBox(int id, int x, int y, int width, int height)
    {
        return AddControl<ListBox>(id, x, y, width, height);
    }
    ScrollBar* Dialog::AddScrollBar(int id, int x, int y, int width, int height)
    {
        return AddControl<ScrollBar>(id, x, y, width, height);
    }
    Control* Dialog::GetControl(int id) const
    {
        for (const auto& control : m_controls)
            if (control->GetId() == id)
                return control.get();
        return nullptr;
    }
    void Dialog::RemoveControl(int id)
    {
        ZoneScoped;
        if (m_focus && m_focus->GetId() == id)
            RequestFocus(nullptr);
        if (m_mouseOver && m_mouseOver->GetId() == id)
            m_mouseOver = nullptr;
        if (m_pressed && m_pressed->GetId() == id)
            m_pressed = nullptr;
        std::erase_if(m_controls, [id](const auto& control) {
            return control->GetId() == id;
        });
    }
    void Dialog::RemoveAllControls()
    {
        RequestFocus(nullptr);
        m_controls.clear();
        m_focus = m_mouseOver = m_pressed = nullptr;
    }
    void Dialog::ClearRadioButtonGroup(unsigned int group, RadioButton* except)
    {
        for (const auto& control : m_controls)
            if (auto* radio = dynamic_cast<RadioButton*>(control.get()); radio && radio != except && radio->GetGroup() == group)
                radio->SetChecked(false);
    }
    void Dialog::RequestFocus(Control* control)
    {
        ZoneScoped;
        if (m_focus == control)
            return;
        if (m_focus)
            m_focus->OnFocus(false);
        m_focus = control;
        if (m_focus)
            m_focus->OnFocus(true);
    }
    Control* Dialog::GetControlAtPoint(int x, int y) const
    {
        for (auto it = m_controls.rbegin(); it != m_controls.rend(); ++it)
            if ((*it)->IsVisible() && (*it)->Contains(x, y))
                return it->get();
        return nullptr;
    }
    bool Dialog::MsgProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        ZoneScoped;
        if (!m_visible)
            return false;

        const bool mouseMessage = message >= WM_MOUSEFIRST && message <= WM_MOUSELAST;
        const bool keyMessage = message == WM_KEYDOWN || message == WM_KEYUP || message == WM_CHAR;
        if (mouseMessage && !m_mouseInput)
            return false;
        if (keyMessage && !m_keyboardInput)
            return false;

        LPARAM routedLParam = lParam;
        if (message == WM_MOUSEWHEEL && window)
        {
            POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            ScreenToClient(window, &point);
            routedLParam = MAKELPARAM(static_cast<short>(point.x), static_cast<short>(point.y));
        }

        const int mouseX = GET_X_LPARAM(routedLParam);
        const int mouseY = GET_Y_LPARAM(routedLParam);

        if (message == WM_CAPTURECHANGED && m_pressed)
        {
            m_pressed->MsgProc(message, wParam, lParam);
            m_pressed->m_pressed = false;
            m_pressed = nullptr;
        }

        if (message == WM_MOUSEMOVE)
        {
            Control* next = GetControlAtPoint(mouseX, mouseY);
            if (m_mouseOver != next)
            {
                if (m_mouseOver)
                    m_mouseOver->m_mouseOver = false;
                m_mouseOver = next;
                if (m_mouseOver)
                    m_mouseOver->m_mouseOver = true;
            }

            if (m_pressed && m_pressed->MsgProc(message, wParam, routedLParam))
                return true;
        }

        if (mouseMessage && m_focus && m_focus != m_pressed && dynamic_cast<ComboBox*>(m_focus) && m_focus->MsgProc(message, wParam, routedLParam))
            return true;

        if (message == WM_LBUTTONDOWN || message == WM_LBUTTONDBLCLK)
        {
            Control* control = GetControlAtPoint(mouseX, mouseY);
            if (control)
            {
                if (control->IsEnabled() && control->GetType() != ControlType::ScrollBar && control->GetType() != ControlType::Static)
                    RequestFocus(control);
                m_pressed = control;
                if (window)
                    SetCapture(window);
                if (control->MsgProc(message, wParam, routedLParam))
                    return true;
            }
        }

        if (message == WM_LBUTTONUP && m_pressed)
        {
            Control* control = m_pressed;
            m_pressed = nullptr;
            if (GetCapture() == window)
                ReleaseCapture();
            if (control->MsgProc(message, wParam, routedLParam))
                return true;
        }

        if (mouseMessage)
        {
            if (Control* control = GetControlAtPoint(mouseX, mouseY); control && control != m_pressed && control->MsgProc(message, wParam, routedLParam))
                return true;
        }

        if (keyMessage && m_focus && m_focus->MsgProc(message, wParam, routedLParam))
            return true;

        return false;
    }

    void Dialog::Render()
    {
        ZoneScoped;
        if (!m_visible)
            return;
        ui::OpacityScope fade(GetOpacity());
        const RECT rect = GetRect();
        if (((m_background[0] | m_background[1] | m_background[2] | m_background[3]) >> 24) != 0)
            DrawGradientQuad(rect, m_background[0], m_background[1], m_background[2], m_background[3]);
        for (const auto& control : m_controls)
        {
            ui::OpacityScope controlFade(this == GetDialog(DialogId::GameUI) ? control->GetOpacity() : 1.0f);
            control->Render();
        }
    }
    void Dialog::SendEvent(unsigned int event, Control* control)
    {
        ZoneScoped;
        if (m_callback && control)
            m_callback(event, control->GetId(), control, m_callbackContext);
    }

    bool Initialize(IDirect3DDevice9* device, const char* assetDirectory)
    {
        ZoneScoped;
        if (!device)
            return false;
        Shutdown();
        D3DCAPS9 caps{};
        if (FAILED(device->GetDeviceCaps(&caps)) || caps.MaxPrimitiveCount < 2)
            return false;
        g_vertexChunkSize = std::min<std::size_t>(caps.MaxPrimitiveCount / 2, 4096) * 6;
        g_device = device;
        g_device->AddRef();
        D3DDEVICE_CREATION_PARAMETERS parameters{};
        if (SUCCEEDED(device->GetCreationParameters(&parameters)))
        {
            g_window = parameters.hFocusWindow;
            ime::SetWindow(g_window);
        }
        if (assetDirectory)
            SetAssetDirectory(assetDirectory);
        SetupDefaultElements();
        const std::string skinPath = AssetPath("sampgui.png");
        const std::string cursorPath = AssetPath("mouse.png");
        const bool skinOk = CreateTextureFromPng(skinPath.c_str(), &g_skin);
        const bool fontOk = CreateFontAtlas();
        const bool cursorOk = CreateCursorTexture(cursorPath.c_str());
        SetupDialogs();
        g_initialized = skinOk && fontOk && cursorOk;
        return g_initialized;
    }

    void Shutdown()
    {
        ZoneScoped;
        ime::Shutdown();
        ReleaseMeasurement();
        for (Dialog& dialog : g_dialogs)
            dialog.Reset();
        Release(g_cursor);
        Release(g_skin);
        g_skinMetrics = {};
        g_cursorMetrics = {};
        Release(g_font.texture);
        for (auto*& state : g_states)
            Release(state);
        g_states.clear();
        g_pendingVertices.clear();
        g_textureDescriptions.clear();
        Release(g_vertexBuffer);
        g_vertexBufferOffset = 0;
        Release(g_device);
        g_window = nullptr;
        g_clientRect = {};
        g_clientDirty = true;
        std::vector<Vertex>().swap(g_textVertices);
        g_vertexChunkSize = 0;
        g_initialized = false;
    }

    void BeginFrame()
    {
        g_clientDirty = true;
    }
    RECT GetClientRect()
    {
        ZoneScoped;
        if (g_clientDirty)
        {
            g_clientDirty = false;
            RECT client{};
            if (g_window && ::GetClientRect(g_window, &client) && client.right > 0 && client.bottom > 0)
                g_clientRect = client;
            else if (g_device)
            {
                D3DVIEWPORT9 viewport{};
                if (SUCCEEDED(g_device->GetViewport(&viewport)))
                    g_clientRect = RECT{0, 0, static_cast<LONG>(viewport.Width), static_cast<LONG>(viewport.Height)};
            }
        }
        return g_clientRect;
    }

    void Render()
    {
        ZoneScoped;

        if (!g_initialized || !g_device)
            return;
        if (g_fontDirty)
            CreateFontAtlas();
        StateGuard state;
        if (!state.IsValid())
            return;
        constexpr DialogId order[] = {DialogId::Scoreboard, DialogId::GameUI, DialogId::ClassSelection, DialogId::Dialog, DialogId::Secondary, DialogId::Auxiliary};
        for (DialogId id : order)
            g_dialogs[Index(id)].Render();
        FlushUi();
        ApplyUiState(g_skin);
        ime::Render(g_device, g_fontFace.c_str(), g_fontHeight, g_fontWeight);
    }

    void RenderCursor(int x, int y)
    {
        ZoneScoped;
        if (!g_initialized || !g_device || !g_cursor)
            return;
        StateGuard state;
        if (!state.IsValid())
            return;
        DrawQuad(RECT{x, y, x + 32, y + 32}, 0xFFFFFFFFu, g_cursor);
    }

    void OnLostDevice()
    {
        ZoneScoped;
        for (auto*& state : g_states)
            Release(state);
        g_states.clear();
        g_pendingVertices.clear();
        g_textureDescriptions.clear();
        Release(g_vertexBuffer);
        g_vertexBufferOffset = 0;
        // skin, font and cursor textures live in MANAGED pool and survive Reset
    }

    void OnResetDevice()
    {
        g_clientDirty = true;
        if (!g_device)
            return;
        D3DVIEWPORT9 viewport{};
        if (SUCCEEDED(g_device->GetViewport(&viewport)))
            g_dialogs[Index(DialogId::GameUI)].SetSize(static_cast<int>(viewport.Width), static_cast<int>(viewport.Height));
    }

    bool MsgProc(DialogId id, HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        const std::size_t index = Index(id);
        return index < g_dialogs.size() && g_dialogs[index].MsgProc(window, message, wParam, lParam);
    }

    bool MsgProcAll(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        ZoneScoped;
        if (message == WM_SIZE)
            g_clientDirty = true;
        // Reverse render order so top-most dialogs get the message first.
        constexpr DialogId order[] = {DialogId::Auxiliary, DialogId::Secondary, DialogId::Dialog, DialogId::ClassSelection, DialogId::GameUI, DialogId::Scoreboard};
        for (DialogId id : order)
            if (MsgProc(id, window, message, wParam, lParam))
                return true;
        return false;
    }

    bool IsInitialized()
    {
        return g_initialized;
    }
    IDirect3DDevice9* GetDevice()
    {
        return g_device;
    }
    IDirect3DTexture9* GetSkinTexture()
    {
        return g_skin;
    }
    IDirect3DTexture9* GetCursorTexture()
    {
        return g_cursor;
    }
    Dialog* GetDialog(DialogId id)
    {
        const std::size_t index = Index(id);
        return index < g_dialogs.size() ? &g_dialogs[index] : nullptr;
    }
    const SkinElement* GetDefaultElement(ControlType type, unsigned int element)
    {
        const std::size_t typeIndex = static_cast<std::size_t>(type);
        if (typeIndex >= g_defaultElements.size())
            return nullptr;
        const auto& list = g_defaultElements[typeIndex];
        return element < list.size() ? &list[element] : nullptr;
    }
    void SetAssetDirectory(const char* directory)
    {
        g_assetDirectory = directory ? directory : "";
    }
    const char* GetAssetDirectory()
    {
        return g_assetDirectory.c_str();
    }
    void SetFont(const char* face, int height, int weight)
    {
        ZoneScoped;
        const std::string nextFace = face && *face ? face : "Arial";
        const int nextHeight = std::clamp(height, 4, 96);
        const int nextWeight = std::clamp(weight, 1, 1000);
        if (g_fontFace == nextFace && g_fontHeight == nextHeight && g_fontWeight == nextWeight)
            return;
        g_fontFace = nextFace;
        g_fontHeight = nextHeight;
        g_fontWeight = nextWeight;
        g_fontDirty = true;
    }
    void ApplyR5FontConfig(int fontSize, int fontWeight, const char* fontFace)
    {
        const int height = fontSize * 2 + 20;
        const int weight = fontWeight == 1 ? FW_NORMAL : FW_BOLD;
        SetFont(fontFace && *fontFace ? fontFace : "Arial", height, weight);
    }
    const char* GetFontFace()
    {
        return g_fontFace.c_str();
    }
    int GetFontHeight()
    {
        return g_fontHeight;
    }
    int GetFontWeight()
    {
        return g_fontWeight;
    }

}
