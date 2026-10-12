#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#   define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#   define NOMINMAX
#endif

#include <Windows.h>
#include <d3d9.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <unordered_map>
#include <vector>

class CFont
{
public:
    CFont(IDirect3DDevice9* device, int height, int width, int weight, const char* face, UINT codePage = CP_ACP, BYTE charSet = DEFAULT_CHARSET,
          DWORD quality = ANTIALIASED_QUALITY);

    ~CFont();

    CFont(const CFont&) = delete;
    CFont& operator=(const CFont&) = delete;

    // only draw through this font until the scope ends
    class Batch
    {
    public:
        explicit Batch(CFont* font, bool renderToTexture = false);
        ~Batch();
        Batch(const Batch&) = delete;
        Batch& operator=(const Batch&) = delete;

    private:
        CFont* m_font;
        bool m_previousRenderToTexture = false;
    };

    bool IsValid() const;

    void OnLostDevice();
    void OnResetDevice();

    int DrawTextA(const char* text, RECT* rect, DWORD format, D3DCOLOR color);
    int DrawTextA(const char* text, RECT* rect, DWORD format, D3DCOLOR color, D3DBLEND destBlend);

    int DrawTextA(const char* text, int count, RECT* rect, DWORD format, D3DCOLOR color);
    int DrawTextA(const char* text, int count, RECT* rect, DWORD format, D3DCOLOR color, D3DBLEND destBlend);

    int DrawTextOutlinedA(const char* text, RECT* rect, DWORD format, D3DCOLOR color, D3DCOLOR outlineColor, int outlineSize);

    void DrawBox(float x, float y, float width, float height, D3DCOLOR color);

    SIZE GetTextSize(const char* text, DWORD format = DT_NOCLIP) const;

    int GetHeight() const
    {
        return m_lineHeight;
    }

    static bool IsColorTag(const char* text);
    static bool ParseColorTag(const char* text, D3DCOLOR& color);
    static void StripColorTags(std::string& text);

private:
    struct Glyph
    {
        bool valid = false;
        int width = 0;
        int height = 0;
        int originX = 0;
        int originY = 0;
        int advance = 0;
        float u0 = 0.0f;
        float v0 = 0.0f;
        float u1 = 0.0f;
        float v1 = 0.0f;
    };

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

    struct StyledChar
    {
        unsigned char value = 0;
        D3DCOLOR color = 0;
    };

    struct Line
    {
        std::size_t begin = 0;
        std::size_t end = 0;
        float width = 0.0f;
    };

    struct SavedState
    {
        IDirect3DBaseTexture9* texture = nullptr;
        IDirect3DBaseTexture9* texture1 = nullptr;
        IDirect3DVertexBuffer9* stream = nullptr;
        IDirect3DIndexBuffer9* indices = nullptr;
        IDirect3DVertexShader9* vertexShader = nullptr;
        IDirect3DPixelShader9* pixelShader = nullptr;
        IDirect3DVertexDeclaration9* declaration = nullptr;
        UINT streamOffset = 0;
        UINT streamStride = 0;
        DWORD fvf = 0;

        DWORD blendOp = 0;
        DWORD colorWriteEnable = 0;
        DWORD alphaBlendEnable = 0;
        DWORD srcBlend = 0;
        DWORD destBlend = 0;
        DWORD separateAlphaBlendEnable = 0;
        DWORD srcBlendAlpha = 0;
        DWORD destBlendAlpha = 0;
        DWORD blendOpAlpha = 0;
        DWORD alphaTestEnable = 0;
        DWORD zEnable = 0;
        DWORD zWriteEnable = 0;
        DWORD cullMode = 0;
        DWORD lighting = 0;
        DWORD fogEnable = 0;
        DWORD scissorTestEnable = 0;

        DWORD colorOp = 0;
        DWORD colorArg1 = 0;
        DWORD colorArg2 = 0;
        DWORD alphaOp = 0;
        DWORD alphaArg1 = 0;
        DWORD alphaArg2 = 0;
        DWORD colorOp1 = 0;
        DWORD alphaOp1 = 0;

        DWORD minFilter = 0;
        DWORD magFilter = 0;
        DWORD mipFilter = 0;

        RECT scissorRect{};
    };

private:
    bool CreateFontObject();
    bool CreateAtlas();
    bool EnsureVertexBuffer(std::size_t vertexCount);
    bool EnsureIndexBuffer();

    wchar_t ByteToWide(unsigned char value) const;
    int GetKerning(unsigned char left, unsigned char right) const;

    void BuildStyledText(const char* text, int count, D3DCOLOR baseColor, bool useColorTags, std::vector<StyledChar>& output) const;

    float MeasureRange(const std::vector<StyledChar>& text, std::size_t begin, std::size_t end, DWORD format) const;

    void BuildLines(const std::vector<StyledChar>& text, float maxWidth, DWORD format, std::vector<Line>& lines) const;

    int BuildVertices(const char* text, int count, RECT rect, DWORD format, D3DCOLOR color, bool useColorTags, float offsetX, float offsetY, std::vector<Vertex>& vertices,
                      SIZE* measured = nullptr) const;

    int BuildVerticesUncached(const char* text, int count, RECT rect, DWORD format, D3DCOLOR color, bool useColorTags, float offsetX, float offsetY, std::vector<Vertex>& vertices,
                              SIZE* measured) const;
    struct CachedText
    {
        std::vector<Vertex> vertices;
        SIZE size{};
        int height = 0;
    };
    mutable std::unordered_map<std::string, CachedText> m_textCache;
    mutable std::deque<std::string> m_textCacheOrder;
    mutable std::size_t m_textCacheBytes = 0;

    int DrawVertices(std::vector<Vertex>& vertices, const RECT* clipRect, bool clip, D3DBLEND destBlend = D3DBLEND_INVSRCALPHA);
    int SubmitVertices(const std::vector<Vertex>& vertices, const RECT* clipRect, bool clip, D3DBLEND destBlend);
    void FlushBatch();

    void SaveState(SavedState& state) const;
    void ApplyFontState(const RECT* clipRect, bool clip, D3DBLEND destBlend) const;
    void RestoreState(SavedState& state) const;

    void ReleaseVertexBuffer();
    void ReleaseAtlas();
    void ReleaseFontObject();

private:
    IDirect3DDevice9* m_device = nullptr;
    IDirect3DTexture9* m_atlas = nullptr;
    IDirect3DVertexBuffer9* m_vertexBuffer = nullptr;
    IDirect3DIndexBuffer9* m_indexBuffer = nullptr;

    HDC m_dc = nullptr;
    HFONT m_font = nullptr;
    HGDIOBJ m_oldFont = nullptr;

    UINT m_codePage = CP_ACP;
    BYTE m_charSet = DEFAULT_CHARSET;
    int m_requestedHeight = 0;
    int m_requestedWidth = 0;
    int m_weight = FW_NORMAL;
    DWORD m_quality = ANTIALIASED_QUALITY;
    std::string m_face;

    int m_lineHeight = 0;
    int m_ascent = 0;
    int m_spaceAdvance = 0;

    UINT m_atlasWidth = 0;
    UINT m_atlasHeight = 0;
    std::size_t m_vertexCapacity = 0;
    std::size_t m_vertexOffset = 0;

    unsigned int m_batchDepth = 0;
    bool m_renderToTexture = false;
    D3DBLEND m_batchDestBlend = D3DBLEND_INVSRCALPHA;
    std::vector<Vertex> m_batchVertices;
    std::vector<Vertex> m_vertices;
    std::vector<Vertex> m_textVertices;
    mutable std::vector<StyledChar> m_styledText;
    mutable std::vector<Line> m_lines;
    std::array<Glyph, 256> m_glyphs{};
    std::unordered_map<std::uint32_t, int> m_kerning;
};

class CConfig;

class CFonts
{
public:
    // The supplied config must outlive these fonts; nullptr uses defaults.
    explicit CFonts(IDirect3DDevice9* device, const CConfig* config = nullptr);
    ~CFonts();

    CFonts(const CFonts&) = delete;
    CFonts& operator=(const CFonts&) = delete;

    void OnLostDevice();
    void OnResetDevice();

    void GetTextScreenSize(SIZE* size, const char* text, int format = 0) const;

    void GetLittleTextScreenSize(SIZE* size, const char* text, int format = 0) const;

    void DrawText(const char* text, RECT rect, D3DCOLOR color, bool shadow = true);

    void DrawLittleText(const char* text, RECT rect, int format, D3DCOLOR color, bool shadow = true);

    void DrawLicensePlateText(const char* text, RECT rect, D3DCOLOR color);

#if defined(_DEBUG)
    void DrawFps(unsigned int fps, double frameTimeMs);
#endif // _DEBUG

    bool Reset();

    int GetCharHeight() const
    {
        return m_charHeight;
    }
    int GetLittleCharHeight() const
    {
        return m_littleCharHeight;
    }

    CFont* GetFont() const
    {
        return m_font;
    }
    CFont* GetLittleFont() const
    {
        return m_littleFont;
    }
    CFont* GetLicensePlateFont() const
    {
        return m_licensePlateFont;
    }

    void GetFontSettings(int& sizeOffset, int& weight, std::string& face) const;

private:
    void ReleaseFonts();
#if defined(SACO_BUILD)
    bool ReadFontSettings(int& sizeOffset, int& weight, std::string& face) const;
#endif // SACO_BUILD
    int GetBaseFontSize() const;

private:
    IDirect3DDevice9* m_device = nullptr;
    const CConfig* m_config = nullptr;
    CFont* m_font = nullptr;
    CFont* m_littleFont = nullptr;
    CFont* m_licensePlateFont = nullptr;
#if defined(_DEBUG)
    CFont* m_fpsFont = nullptr;
#endif // _DEBUG
    int m_charHeight = 0;
    int m_littleCharHeight = 0;
};
