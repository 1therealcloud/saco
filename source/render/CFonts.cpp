#if defined(SACO_BUILD)
#   include <stdafx.h>
#else // SACO_BUILD
#   include "utils/log.inl"
#endif // SACO_BUILD

#include "CFonts.h"
#include "gui/CGui.h"

#if defined(SACO_BUILD)
#   include "config/CConfig.h"
#endif // SACO_BUILD

#include <cmath>

namespace
{
    constexpr std::size_t kMaxTextLength = 100000;
    constexpr DWORD kFontFvf = D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1;
    constexpr int kGlyphPadding = 1;
    constexpr int kTabSpaces = 8;

    template <typename T>
    void SafeRelease(T*& object)
    {
        if (!object)
            return;

        object->Release();
        object = nullptr;
    }

    UINT NextPowerOfTwo(UINT value)
    {
        if (value <= 1)
            return 1;

        --value;
        value |= value >> 1;
        value |= value >> 2;
        value |= value >> 4;
        value |= value >> 8;
        value |= value >> 16;
        return value + 1;
    }

    bool IsHex(char value)
    {
        return (value >= '0' && value <= '9') || (value >= 'A' && value <= 'F') || (value >= 'a' && value <= 'f');
    }

    unsigned HexValue(char value)
    {
        if (value >= '0' && value <= '9')
            return static_cast<unsigned>(value - '0');

        if (value >= 'A' && value <= 'F')
            return static_cast<unsigned>(value - 'A' + 10);

        return static_cast<unsigned>(value - 'a' + 10);
    }

    bool IsLineBreak(unsigned char value)
    {
        return value == '\n' || value == '\r';
    }

    bool IsSpace(unsigned char value)
    {
        return value == ' ' || value == '\t';
    }

    struct RasterGlyph
    {
        bool valid = false;
        int width = 0;
        int height = 0;
        int originX = 0;
        int originY = 0;
        int advance = 0;
        UINT pitch = 0;
        std::vector<std::uint8_t> bitmap;
    };
}

CFont::CFont(IDirect3DDevice9* device, int height, int width, int weight, const char* face, UINT codePage, BYTE charSet, DWORD quality)
    : m_device(device), m_codePage(codePage), m_charSet(charSet), m_requestedHeight(height), m_requestedWidth(width), m_weight(weight), m_quality(quality),
      m_face(face && *face ? face : "Arial")
{
    if (m_device)
        m_device->AddRef();

    if (!CreateFontObject() || !CreateAtlas())
    {
        ReleaseAtlas();
        ReleaseFontObject();
    }
}

CFont::~CFont()
{
    ReleaseVertexBuffer();
    ReleaseAtlas();
    ReleaseFontObject();
    SafeRelease(m_device);
}

bool CFont::IsValid() const
{
    return m_device && m_font && m_dc && m_atlas && m_lineHeight > 0;
}

void CFont::OnLostDevice()
{
    ReleaseVertexBuffer();
}

void CFont::OnResetDevice()
{
    // the atlas lives in D3DPOOL_MANAGED; the dynamic VB is recreated lazily.
}

bool CFont::CreateFontObject()
{
    if (!m_device)
        return false;

    m_dc = CreateCompatibleDC(nullptr);
    if (!m_dc)
        return false;

    m_font = CreateFontA(m_requestedHeight, m_requestedWidth, 0, 0, m_weight, FALSE, FALSE, FALSE, m_charSet, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, m_quality, DEFAULT_PITCH,
                         m_face.c_str());

    if (!m_font)
        return false;

    m_oldFont = SelectObject(m_dc, m_font);
    if (!m_oldFont || m_oldFont == HGDI_ERROR)
        return false;

    TEXTMETRICA metrics{};
    if (!GetTextMetricsA(m_dc, &metrics))
        return false;

    m_lineHeight = std::max(1, static_cast<int>(metrics.tmHeight));
    m_ascent = std::max(0, static_cast<int>(metrics.tmAscent));

    const wchar_t space = L' ';
    GLYPHMETRICS glyphMetrics{};
    MAT2 matrix{};
    matrix.eM11.value = 1;
    matrix.eM22.value = 1;

    if (GetGlyphOutlineW(m_dc, space, GGO_METRICS, &glyphMetrics, 0, nullptr, &matrix) != GDI_ERROR)
    {
        m_spaceAdvance = std::max(1, static_cast<int>(glyphMetrics.gmCellIncX));
    }
    else
    {
        SIZE size{};
        GetTextExtentPoint32W(m_dc, &space, 1, &size);
        m_spaceAdvance = std::max(1, static_cast<int>(size.cx));
    }

    return true;
}

wchar_t CFont::ByteToWide(unsigned char value) const
{
    if (m_charSet == SYMBOL_CHARSET)
        return static_cast<wchar_t>(0xF000u | value);
    if (value < 0x80)
        return static_cast<wchar_t>(value);

    char input[2] = {static_cast<char>(value), 0};
    wchar_t output = L'?';

    if (MultiByteToWideChar(m_codePage, MB_PRECOMPOSED, input, 1, &output, 1) == 1)
    {
        return output;
    }

    return static_cast<wchar_t>(value);
}

bool CFont::CreateAtlas()
{
    if (!m_dc || !m_font || !m_device)
        return false;

    std::array<RasterGlyph, 256> rasters{};

    MAT2 matrix{};
    matrix.eM11.value = 1;
    matrix.eM22.value = 1;

    UINT largestGlyphWidth = 1;
    UINT totalArea = 1;

    for (unsigned i = 0; i < rasters.size(); ++i)
    {
        RasterGlyph& raster = rasters[i];
        const wchar_t character = ByteToWide(static_cast<unsigned char>(i));
        wchar_t rasterCharacter = character;

        GLYPHMETRICS metrics{};
        DWORD size = GetGlyphOutlineW(m_dc, rasterCharacter, GGO_GRAY8_BITMAP, &metrics, 0, nullptr, &matrix);

        if (size == GDI_ERROR && rasterCharacter != L'?')
        {
            rasterCharacter = L'?';
            size = GetGlyphOutlineW(m_dc, rasterCharacter, GGO_GRAY8_BITMAP, &metrics, 0, nullptr, &matrix);
        }

        if (size == GDI_ERROR)
            continue;

        raster.valid = true;
        raster.width = static_cast<int>(metrics.gmBlackBoxX);
        raster.height = static_cast<int>(metrics.gmBlackBoxY);
        raster.originX = metrics.gmptGlyphOrigin.x;
        raster.originY = metrics.gmptGlyphOrigin.y;
        raster.advance = static_cast<int>(metrics.gmCellIncX);

        if (raster.advance <= 0)
            raster.advance = m_spaceAdvance;

        if (raster.width <= 0 || raster.height <= 0 || size == 0)
            continue;

        raster.pitch = (static_cast<UINT>(raster.width) + 3u) & ~3u;
        raster.bitmap.resize(size);

        if (GetGlyphOutlineW(m_dc, rasterCharacter, GGO_GRAY8_BITMAP, &metrics, size, raster.bitmap.data(), &matrix) == GDI_ERROR)
        {
            raster.bitmap.clear();
            raster.width = 0;
            raster.height = 0;
            continue;
        }

        const UINT paddedWidth = static_cast<UINT>(raster.width + kGlyphPadding * 2);
        const UINT paddedHeight = static_cast<UINT>(raster.height + kGlyphPadding * 2);

        largestGlyphWidth = std::max(largestGlyphWidth, paddedWidth);
        totalArea += paddedWidth * paddedHeight;
    }

    D3DCAPS9 caps{};
    if (FAILED(m_device->GetDeviceCaps(&caps)))
        return false;

    UINT atlasWidth = 256;
    while (atlasWidth < largestGlyphWidth)
        atlasWidth <<= 1;

    while (atlasWidth < 2048 && atlasWidth * atlasWidth < totalArea * 2)
        atlasWidth <<= 1;

    atlasWidth = std::min(atlasWidth, static_cast<UINT>(caps.MaxTextureWidth));
    if (atlasWidth < largestGlyphWidth)
        return false;

    UINT x = kGlyphPadding;
    UINT y = kGlyphPadding;
    UINT rowHeight = 0;
    UINT requiredHeight = 1;

    std::array<POINT, 256> positions{};

    for (unsigned i = 0; i < rasters.size(); ++i)
    {
        const RasterGlyph& raster = rasters[i];
        if (!raster.valid || raster.width <= 0 || raster.height <= 0)
            continue;

        const UINT width = static_cast<UINT>(raster.width + kGlyphPadding * 2);
        const UINT height = static_cast<UINT>(raster.height + kGlyphPadding * 2);

        if (x + width > atlasWidth)
        {
            x = kGlyphPadding;
            y += rowHeight;
            rowHeight = 0;
        }

        positions[i].x = static_cast<LONG>(x + kGlyphPadding);
        positions[i].y = static_cast<LONG>(y + kGlyphPadding);

        x += width;
        rowHeight = std::max(rowHeight, height);
        requiredHeight = std::max(requiredHeight, y + rowHeight + kGlyphPadding);
    }

    const UINT atlasHeight = NextPowerOfTwo(requiredHeight);
    if (atlasHeight > caps.MaxTextureHeight)
        return false;

    ReleaseAtlas();

    if (FAILED(m_device->CreateTexture(atlasWidth, atlasHeight, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &m_atlas, nullptr)))
    {
        return false;
    }

    D3DLOCKED_RECT locked{};
    if (FAILED(m_atlas->LockRect(0, &locked, nullptr, 0)))
    {
        ReleaseAtlas();
        return false;
    }

    for (UINT row = 0; row < atlasHeight; ++row)
    {
        std::memset(static_cast<std::uint8_t*>(locked.pBits) + row * locked.Pitch, 0, atlasWidth * sizeof(std::uint32_t));
    }

    for (unsigned i = 0; i < rasters.size(); ++i)
    {
        const RasterGlyph& raster = rasters[i];
        Glyph& glyph = m_glyphs[i];

        glyph = {};
        glyph.valid = raster.valid;
        glyph.width = raster.width;
        glyph.height = raster.height;
        glyph.originX = raster.originX;
        glyph.originY = raster.originY;
        glyph.advance = raster.advance;

        if (!raster.valid || raster.width <= 0 || raster.height <= 0 || raster.bitmap.empty())
            continue;

        const UINT dstX = static_cast<UINT>(positions[i].x);
        const UINT dstY = static_cast<UINT>(positions[i].y);

        for (int row = 0; row < raster.height; ++row)
        {
            auto* destination = reinterpret_cast<std::uint32_t*>(static_cast<std::uint8_t*>(locked.pBits) + (dstY + static_cast<UINT>(row)) * locked.Pitch) + dstX;

            const auto* source = raster.bitmap.data() + static_cast<std::size_t>(row) * raster.pitch;

            for (int column = 0; column < raster.width; ++column)
            {
                const unsigned coverage = std::min<unsigned>(source[column], 64u);
                const unsigned alpha = (coverage * 255u + 32u) / 64u;
                destination[column] = (alpha << 24) | 0x00FFFFFFu;
            }
        }

        glyph.u0 = static_cast<float>(dstX) / static_cast<float>(atlasWidth);
        glyph.v0 = static_cast<float>(dstY) / static_cast<float>(atlasHeight);
        glyph.u1 = static_cast<float>(dstX + raster.width) / static_cast<float>(atlasWidth);
        glyph.v1 = static_cast<float>(dstY + raster.height) / static_cast<float>(atlasHeight);
    }

    m_atlas->UnlockRect(0);

    m_atlasWidth = atlasWidth;
    m_atlasHeight = atlasHeight;

    const DWORD kerningCount = GetKerningPairsW(m_dc, 0, nullptr);
    if (kerningCount)
    {
        std::vector<KERNINGPAIR> pairs(kerningCount);
        const DWORD read = GetKerningPairsW(m_dc, kerningCount, pairs.data());

        for (DWORD i = 0; i < read; ++i)
        {
            const std::uint32_t key = (static_cast<std::uint32_t>(pairs[i].wFirst) << 16) | static_cast<std::uint32_t>(pairs[i].wSecond);

            m_kerning[key] = pairs[i].iKernAmount;
        }
    }

    return true;
}

int CFont::GetKerning(unsigned char left, unsigned char right) const
{
    if (!left || !right || m_kerning.empty())
        return 0;

    const std::uint32_t key = (static_cast<std::uint32_t>(ByteToWide(left)) << 16) | static_cast<std::uint32_t>(ByteToWide(right));

    const auto it = m_kerning.find(key);
    return it != m_kerning.end() ? it->second : 0;
}

bool CFont::IsColorTag(const char* text)
{
    if (!text || text[0] != '{')
        return false;

    for (int i = 1; i <= 6; ++i)
    {
        if (!text[i] || !IsHex(text[i]))
            return false;
    }

    return text[7] == '}';
}

bool CFont::ParseColorTag(const char* text, D3DCOLOR& color)
{
    if (!IsColorTag(text))
        return false;

    std::uint32_t rgb = 0;

    for (int i = 1; i <= 6; ++i)
        rgb = (rgb << 4) | HexValue(text[i]);

    // sa-mp forces alpha to 0xff for colors coming from {RRGGBB}.
    color = 0xFF000000u | rgb;
    return true;
}

void CFont::StripColorTags(std::string& text)
{
    std::size_t write = 0;

    for (std::size_t read = 0; read < text.size();)
    {
        if (read + 8 <= text.size() && IsColorTag(text.c_str() + read))
        {
            read += 8;
            continue;
        }

        text[write++] = text[read++];
    }

    text.resize(write);
}

void CFont::BuildStyledText(const char* text, int count, D3DCOLOR baseColor, bool useColorTags, std::vector<StyledChar>& output) const
{
    output.clear();

    if (!text)
        return;

    std::size_t length = 0;

    if (count < 0)
    {
        length = std::strlen(text);
    }
    else
    {
        while (length < static_cast<std::size_t>(count) && text[length])
            ++length;
    }

    if (length > kMaxTextLength)
        return;

    output.reserve(length);

    D3DCOLOR currentColor = baseColor;

    for (std::size_t i = 0; i < length;)
    {
        if (i + 8 <= length && IsColorTag(text + i))
        {
            if (useColorTags)
                ParseColorTag(text + i, currentColor);

            i += 8;
            continue;
        }

        StyledChar item{};
        item.value = static_cast<unsigned char>(text[i]);
        item.color = currentColor;
        output.push_back(item);
        ++i;
    }
}

float CFont::MeasureRange(const std::vector<StyledChar>& text, std::size_t begin, std::size_t end, DWORD format) const
{
    float width = 0.0f;
    unsigned char previous = 0;

    for (std::size_t i = begin; i < end; ++i)
    {
        unsigned char value = text[i].value;

        if (value == '\r')
            continue;

        if (value == '\n')
        {
            if (format & DT_SINGLELINE)
                value = ' ';
            else
                break;
        }

        if (value == '\t')
        {
            const int tabWidth = std::max(1, m_spaceAdvance * kTabSpaces);
            const int current = static_cast<int>(std::floor(width));
            width = static_cast<float>(((current / tabWidth) + 1) * tabWidth);
            previous = 0;
            continue;
        }

        const Glyph& glyph = m_glyphs[value];
        if (!glyph.valid)
            continue;

        if (previous)
            width += static_cast<float>(GetKerning(previous, value));

        width += static_cast<float>(glyph.advance);
        previous = value;
    }

    return std::max(0.0f, width);
}

void CFont::BuildLines(const std::vector<StyledChar>& text, float maxWidth, DWORD format, std::vector<Line>& lines) const
{
    lines.clear();

    if (text.empty())
    {
        lines.push_back({});
        return;
    }

    const bool singleLine = (format & DT_SINGLELINE) != 0;
    const bool wordBreak = !singleLine && (format & DT_WORDBREAK) != 0 && maxWidth > 0.0f;

    std::size_t lineStart = 0;
    std::size_t i = 0;

    while (i < text.size())
    {
        if (!singleLine && text[i].value == '\n')
        {
            Line line{};
            line.begin = lineStart;
            line.end = i;
            line.width = MeasureRange(text, line.begin, line.end, format);
            lines.push_back(line);

            ++i;
            lineStart = i;
            continue;
        }

        if (!singleLine && text[i].value == '\r')
        {
            ++i;
            continue;
        }

        if (wordBreak)
        {
            const float width = MeasureRange(text, lineStart, i + 1, format);

            if (width > maxWidth && i > lineStart)
            {
                std::size_t breakAt = i;

                for (std::size_t j = i; j > lineStart; --j)
                {
                    if (IsSpace(text[j - 1].value))
                    {
                        breakAt = j - 1;
                        break;
                    }
                }

                if (breakAt == lineStart)
                    breakAt = i;

                Line line{};
                line.begin = lineStart;
                line.end = breakAt;
                line.width = MeasureRange(text, line.begin, line.end, format);
                lines.push_back(line);

                lineStart = breakAt;
                while (lineStart < text.size() && IsSpace(text[lineStart].value))
                    ++lineStart;

                i = lineStart;
                continue;
            }
        }

        ++i;
    }

    Line line{};
    line.begin = lineStart;
    line.end = text.size();
    line.width = MeasureRange(text, line.begin, line.end, format);
    lines.push_back(line);
}

int CFont::BuildVertices(const char* text, int count, RECT rect, DWORD format, D3DCOLOR color, bool useColorTags, float offsetX, float offsetY, std::vector<Vertex>& vertices,
                         SIZE* measured) const
{
    auto& styled = m_styledText;
    BuildStyledText(text, count, color, useColorTags, styled);

    if (!text || std::strlen(text) > kMaxTextLength)
    {
        if (measured)
            *measured = {};
        return 0;
    }

    const float rectWidth = static_cast<float>(std::max<LONG>(0, rect.right - rect.left));

    auto& lines = m_lines;
    BuildLines(styled, rectWidth, format, lines);

    float measuredWidth = 0.0f;
    for (const Line& line : lines)
        measuredWidth = std::max(measuredWidth, line.width);

    const float measuredHeight = static_cast<float>(std::max<std::size_t>(1, lines.size()) * m_lineHeight);

    if (measured)
    {
        measured->cx = static_cast<LONG>(std::ceil(measuredWidth));
        measured->cy = static_cast<LONG>(std::ceil(measuredHeight));
    }

    if (format & DT_CALCRECT)
        return static_cast<int>(std::ceil(measuredHeight));

    float startY = static_cast<float>(rect.top) + offsetY;
    const float rectHeight = static_cast<float>(rect.bottom - rect.top);

    if (format & DT_VCENTER)
        startY += (rectHeight - measuredHeight) * 0.5f;
    else if (format & DT_BOTTOM)
        startY += rectHeight - measuredHeight;

    for (std::size_t lineIndex = 0; lineIndex < lines.size(); ++lineIndex)
    {
        const Line& line = lines[lineIndex];
        float penX = static_cast<float>(rect.left) + offsetX;

        if (format & DT_CENTER)
            penX += (rectWidth - line.width) * 0.5f;
        else if (format & DT_RIGHT)
            penX += rectWidth - line.width;

        const float lineTop = startY + static_cast<float>(lineIndex * m_lineHeight);
        const float baseline = lineTop + static_cast<float>(m_ascent);
        unsigned char previous = 0;

        for (std::size_t i = line.begin; i < line.end; ++i)
        {
            unsigned char value = styled[i].value;

            if (value == '\r')
                continue;

            if (value == '\n')
            {
                if (format & DT_SINGLELINE)
                    value = ' ';
                else
                    continue;
            }

            if (value == '\t')
            {
                const int tabWidth = std::max(1, m_spaceAdvance * kTabSpaces);
                const float relative = penX - static_cast<float>(rect.left) - offsetX;
                const int current = static_cast<int>(std::floor(std::max(0.0f, relative)));
                penX = static_cast<float>(rect.left) + offsetX + static_cast<float>(((current / tabWidth) + 1) * tabWidth);
                previous = 0;
                continue;
            }

            const Glyph& glyph = m_glyphs[value];
            if (!glyph.valid)
                continue;

            if (previous)
                penX += static_cast<float>(GetKerning(previous, value));

            if (glyph.width > 0 && glyph.height > 0)
            {
                const float left = penX + static_cast<float>(glyph.originX) - 0.5f;
                const float top = baseline - static_cast<float>(glyph.originY) - 0.5f;
                const float right = left + static_cast<float>(glyph.width);
                const float bottom = top + static_cast<float>(glyph.height);
                const D3DCOLOR glyphColor = styled[i].color;

                vertices.push_back({left, top, 0.0f, 1.0f, glyphColor, glyph.u0, glyph.v0});
                vertices.push_back({right, top, 0.0f, 1.0f, glyphColor, glyph.u1, glyph.v0});
                vertices.push_back({left, bottom, 0.0f, 1.0f, glyphColor, glyph.u0, glyph.v1});

                vertices.push_back({right, bottom, 0.0f, 1.0f, glyphColor, glyph.u1, glyph.v1});
            }

            penX += static_cast<float>(glyph.advance);
            previous = value;
        }
    }

    return static_cast<int>(std::ceil(measuredHeight));
}

bool CFont::EnsureVertexBuffer(std::size_t vertexCount)
{
    if (!m_device)
        return false;

    if (m_vertexBuffer && m_vertexCapacity >= vertexCount)
        return true;

    ReleaseVertexBuffer();

    std::size_t capacity = 16384;
    while (capacity < vertexCount)
        capacity *= 2;

    if (capacity > std::numeric_limits<UINT>::max() / sizeof(Vertex))
        return false;

    const UINT size = static_cast<UINT>(capacity * sizeof(Vertex));

    if (FAILED(m_device->CreateVertexBuffer(size, D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, kFontFvf, D3DPOOL_DEFAULT, &m_vertexBuffer, nullptr)))
    {
        return false;
    }

    m_vertexCapacity = capacity;
    return true;
}

bool CFont::EnsureIndexBuffer()
{
    if (m_indexBuffer)
        return true;
    constexpr UINT quadCount = 4096;
    constexpr UINT indexCount = quadCount * 6;
    if (FAILED(m_device->CreateIndexBuffer(indexCount * sizeof(WORD), D3DUSAGE_WRITEONLY, D3DFMT_INDEX16, D3DPOOL_DEFAULT, &m_indexBuffer, nullptr)))
        return false;
    void* data = nullptr;
    if (FAILED(m_indexBuffer->Lock(0, 0, &data, 0)))
    {
        SafeRelease(m_indexBuffer);
        return false;
    }
    auto* indices = static_cast<WORD*>(data);
    for (UINT quad = 0; quad < quadCount; ++quad)
    {
        const WORD base = static_cast<WORD>(quad * 4);
        *indices++ = base;
        *indices++ = base + 1;
        *indices++ = base + 2;
        *indices++ = base + 1;
        *indices++ = base + 3;
        *indices++ = base + 2;
    }
    m_indexBuffer->Unlock();
    return true;
}

void CFont::SaveState(SavedState& state) const
{
    if (!m_device)
        return;

    m_device->GetTexture(0, &state.texture);
    m_device->GetTexture(1, &state.texture1);
    m_device->GetStreamSource(0, &state.stream, &state.streamOffset, &state.streamStride);
    m_device->GetIndices(&state.indices);
    m_device->GetVertexShader(&state.vertexShader);
    m_device->GetPixelShader(&state.pixelShader);
    m_device->GetVertexDeclaration(&state.declaration);
    m_device->GetFVF(&state.fvf);

    m_device->GetRenderState(D3DRS_ALPHABLENDENABLE, &state.alphaBlendEnable);
    m_device->GetRenderState(D3DRS_SRCBLEND, &state.srcBlend);
    m_device->GetRenderState(D3DRS_DESTBLEND, &state.destBlend);
    m_device->GetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, &state.separateAlphaBlendEnable);
    m_device->GetRenderState(D3DRS_ALPHATESTENABLE, &state.alphaTestEnable);
    m_device->GetRenderState(D3DRS_ZENABLE, &state.zEnable);
    m_device->GetRenderState(D3DRS_ZWRITEENABLE, &state.zWriteEnable);
    m_device->GetRenderState(D3DRS_CULLMODE, &state.cullMode);
    m_device->GetRenderState(D3DRS_LIGHTING, &state.lighting);
    m_device->GetRenderState(D3DRS_FOGENABLE, &state.fogEnable);
    m_device->GetRenderState(D3DRS_SCISSORTESTENABLE, &state.scissorTestEnable);

    m_device->GetTextureStageState(0, D3DTSS_COLOROP, &state.colorOp);
    m_device->GetTextureStageState(0, D3DTSS_COLORARG1, &state.colorArg1);
    m_device->GetTextureStageState(0, D3DTSS_COLORARG2, &state.colorArg2);
    m_device->GetTextureStageState(0, D3DTSS_ALPHAOP, &state.alphaOp);
    m_device->GetTextureStageState(0, D3DTSS_ALPHAARG1, &state.alphaArg1);
    m_device->GetTextureStageState(0, D3DTSS_ALPHAARG2, &state.alphaArg2);
    m_device->GetTextureStageState(1, D3DTSS_COLOROP, &state.colorOp1);
    m_device->GetTextureStageState(1, D3DTSS_ALPHAOP, &state.alphaOp1);

    m_device->GetSamplerState(0, D3DSAMP_MINFILTER, &state.minFilter);
    m_device->GetSamplerState(0, D3DSAMP_MAGFILTER, &state.magFilter);
    m_device->GetSamplerState(0, D3DSAMP_MIPFILTER, &state.mipFilter);
    m_device->GetScissorRect(&state.scissorRect);
}

void CFont::ApplyFontState(const RECT* clipRect, bool clip, D3DBLEND destBlend) const
{
    m_device->SetVertexShader(nullptr);
    m_device->SetPixelShader(nullptr);
    m_device->SetFVF(kFontFvf);

    m_device->SetTexture(0, m_atlas);
    m_device->SetTexture(1, nullptr);

    m_device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    m_device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    m_device->SetRenderState(D3DRS_DESTBLEND, destBlend);
    m_device->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, FALSE);
    m_device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    m_device->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
    m_device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    m_device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    m_device->SetRenderState(D3DRS_LIGHTING, FALSE);
    m_device->SetRenderState(D3DRS_FOGENABLE, FALSE);

    m_device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    m_device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    m_device->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    m_device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
    m_device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    m_device->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
    m_device->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    m_device->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);

    m_device->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
    m_device->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
    m_device->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);

    if (clip && clipRect)
    {
        m_device->SetScissorRect(clipRect);
        m_device->SetRenderState(D3DRS_SCISSORTESTENABLE, TRUE);
    }
    else
    {
        m_device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    }
}

void CFont::RestoreState(SavedState& state) const
{
    if (!m_device)
        return;

    m_device->SetTexture(0, state.texture);
    m_device->SetTexture(1, state.texture1);
    m_device->SetStreamSource(0, state.stream, state.streamOffset, state.streamStride);
    m_device->SetIndices(state.indices);
    m_device->SetVertexShader(state.vertexShader);
    m_device->SetPixelShader(state.pixelShader);

    if (state.declaration)
        m_device->SetVertexDeclaration(state.declaration);
    else
        m_device->SetFVF(state.fvf);

    m_device->SetRenderState(D3DRS_ALPHABLENDENABLE, state.alphaBlendEnable);
    m_device->SetRenderState(D3DRS_SRCBLEND, state.srcBlend);
    m_device->SetRenderState(D3DRS_DESTBLEND, state.destBlend);
    m_device->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, state.separateAlphaBlendEnable);
    m_device->SetRenderState(D3DRS_ALPHATESTENABLE, state.alphaTestEnable);
    m_device->SetRenderState(D3DRS_ZENABLE, state.zEnable);
    m_device->SetRenderState(D3DRS_ZWRITEENABLE, state.zWriteEnable);
    m_device->SetRenderState(D3DRS_CULLMODE, state.cullMode);
    m_device->SetRenderState(D3DRS_LIGHTING, state.lighting);
    m_device->SetRenderState(D3DRS_FOGENABLE, state.fogEnable);
    m_device->SetRenderState(D3DRS_SCISSORTESTENABLE, state.scissorTestEnable);

    m_device->SetTextureStageState(0, D3DTSS_COLOROP, state.colorOp);
    m_device->SetTextureStageState(0, D3DTSS_COLORARG1, state.colorArg1);
    m_device->SetTextureStageState(0, D3DTSS_COLORARG2, state.colorArg2);
    m_device->SetTextureStageState(0, D3DTSS_ALPHAOP, state.alphaOp);
    m_device->SetTextureStageState(0, D3DTSS_ALPHAARG1, state.alphaArg1);
    m_device->SetTextureStageState(0, D3DTSS_ALPHAARG2, state.alphaArg2);
    m_device->SetTextureStageState(1, D3DTSS_COLOROP, state.colorOp1);
    m_device->SetTextureStageState(1, D3DTSS_ALPHAOP, state.alphaOp1);

    m_device->SetSamplerState(0, D3DSAMP_MINFILTER, state.minFilter);
    m_device->SetSamplerState(0, D3DSAMP_MAGFILTER, state.magFilter);
    m_device->SetSamplerState(0, D3DSAMP_MIPFILTER, state.mipFilter);
    m_device->SetScissorRect(&state.scissorRect);

    SafeRelease(state.texture);
    SafeRelease(state.texture1);
    SafeRelease(state.stream);
    SafeRelease(state.indices);
    SafeRelease(state.vertexShader);
    SafeRelease(state.pixelShader);
    SafeRelease(state.declaration);
}

CFont::Batch::Batch(CFont* font) : m_font(font)
{
    if (m_font)
        ++m_font->m_batchDepth;
}

CFont::Batch::~Batch()
{
    if (m_font && --m_font->m_batchDepth == 0)
        m_font->FlushBatch();
}

void CFont::FlushBatch()
{
    if (m_batchVertices.empty())
        return;
    SubmitVertices(m_batchVertices, nullptr, false, m_batchDestBlend);
    m_batchVertices.clear();
}

int CFont::DrawVertices(std::vector<Vertex>& vertices, const RECT* clipRect, bool clip, D3DBLEND destBlend)
{
    if (vertices.empty() || !IsValid())
        return 0;

    if (ui::opacity < 1.0f)
    {
        for (Vertex& vertex : vertices)
            vertex.color = ui::FadeColor(vertex.color);
    }

    constexpr std::size_t maxBatchVertices = 65536;
    if (m_batchDepth && !clip && vertices.size() <= maxBatchVertices)
    {
        if (!m_batchVertices.empty() && m_batchDestBlend != destBlend)
            FlushBatch();
        m_batchDestBlend = destBlend;
        if (vertices.size() > maxBatchVertices - m_batchVertices.size())
            FlushBatch();
        m_batchVertices.insert(m_batchVertices.end(), vertices.begin(), vertices.end());
        return 1;
    }
    // clipped draws keep their own scissor rectangle and preserve submission order
    FlushBatch();
    return SubmitVertices(vertices, clipRect, clip, destBlend);
}

int CFont::SubmitVertices(const std::vector<Vertex>& vertices, const RECT* clipRect, bool clip, D3DBLEND destBlend)
{
    if (!EnsureVertexBuffer(vertices.size()) || !EnsureIndexBuffer())
        return 0;
    void* data = nullptr;
    const UINT size = static_cast<UINT>(vertices.size() * sizeof(Vertex));

    // append without overwriting queued draws, discard only when the buffer wraps
    const bool discard = m_vertexOffset == 0 || vertices.size() > m_vertexCapacity - m_vertexOffset;
    const std::size_t firstVertex = discard ? 0 : m_vertexOffset;
    const UINT offset = static_cast<UINT>(firstVertex * sizeof(Vertex));
    if (FAILED(m_vertexBuffer->Lock(offset, size, &data, discard ? D3DLOCK_DISCARD : D3DLOCK_NOOVERWRITE)))
        return 0;

    // locked dynamic buffers may use write-combined memory, so only copy into them
    std::memcpy(data, vertices.data(), size);
    m_vertexBuffer->Unlock();
    m_vertexOffset = firstVertex + vertices.size();

    SavedState state{};
    SaveState(state);
    ApplyFontState(clipRect, clip, destBlend);

    m_device->SetStreamSource(0, m_vertexBuffer, 0, sizeof(Vertex));
    m_device->SetIndices(m_indexBuffer);
    HRESULT result = S_OK;
    for (std::size_t begin = 0; begin < vertices.size() && SUCCEEDED(result); begin += 16384)
    {
        const UINT count = static_cast<UINT>(std::min<std::size_t>(16384, vertices.size() - begin));
        result = m_device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, static_cast<INT>(firstVertex + begin), 0, count, 0, count / 2);
    }

    RestoreState(state);
    return SUCCEEDED(result) ? 1 : 0;
}

int CFont::DrawTextA(const char* text, RECT* rect, DWORD format, D3DCOLOR color)
{
    return DrawTextA(text, -1, rect, format, color, D3DBLEND_INVSRCALPHA);
}

int CFont::DrawTextA(const char* text, RECT* rect, DWORD format, D3DCOLOR color, D3DBLEND destBlend)
{
    return DrawTextA(text, -1, rect, format, color, destBlend);
}

int CFont::DrawTextA(const char* text, int count, RECT* rect, DWORD format, D3DCOLOR color)
{
    return DrawTextA(text, count, rect, format, color, D3DBLEND_INVSRCALPHA);
}

int CFont::DrawTextA(const char* text, int count, RECT* rect, DWORD format, D3DCOLOR color, D3DBLEND destBlend)
{
    if (!IsValid() || !text || !rect)
        return 0;

    const std::size_t length = std::strlen(text);
    if (length > kMaxTextLength)
        return 0;

    auto& vertices = m_vertices;
    vertices.clear();
    vertices.reserve(length * 4);

    SIZE measured{};
    const int height = BuildVertices(text, count, *rect, format, color, true, 0.0f, 0.0f, vertices, &measured);

    if (format & DT_CALCRECT)
    {
        rect->right = rect->left + measured.cx;
        rect->bottom = rect->top + measured.cy;
        return height;
    }

    const bool clip = (format & DT_NOCLIP) == 0;
    DrawVertices(vertices, rect, clip, destBlend);
    return height;
}

int CFont::DrawTextOutlinedA(const char* text, RECT* rect, DWORD format, D3DCOLOR color, D3DCOLOR outlineColor, int outlineSize)
{
    if (!IsValid() || !text || !rect)
        return 0;

    const std::size_t length = std::strlen(text);
    if (length > kMaxTextLength)
        return 0;

    if (outlineSize <= 0)
        return DrawTextA(text, rect, format, color);

    auto& vertices = m_vertices;
    vertices.clear();
    vertices.reserve(length * 4 * 5);

    SIZE measured{};

    auto& textVertices = m_textVertices;
    textVertices.clear();
    textVertices.reserve(length * 4);
    const int height = BuildVertices(text, -1, *rect, format, color, true, 0.0f, 0.0f, textVertices, &measured);

    if (format & DT_CALCRECT)
    {
        rect->right = rect->left + measured.cx;
        rect->bottom = rect->top + measured.cy;
        return height;
    }

    // keep r5's outline order while reusing the same text layout
    const float offset = static_cast<float>(outlineSize);
    const std::array<POINT, 4> directions{{{0, -1}, {0, 1}, {-1, 0}, {1, 0}}};
    for (const POINT direction : directions)
    {
        for (Vertex vertex : textVertices)
        {
            vertex.x += direction.x * offset;
            vertex.y += direction.y * offset;
            vertex.color = outlineColor;
            vertices.push_back(vertex);
        }
    }
    vertices.insert(vertices.end(), textVertices.begin(), textVertices.end());

    const bool clip = (format & DT_NOCLIP) == 0;
    DrawVertices(vertices, rect, clip);
    return height;
}

SIZE CFont::GetTextSize(const char* text, DWORD format) const
{
    SIZE size{};

    if (!IsValid() || !text || std::strlen(text) > kMaxTextLength)
        return size;

    RECT rect{};
    std::vector<Vertex> vertices;

    BuildVertices(text, -1, rect, format | DT_CALCRECT, 0xFF000000u, false, 0.0f, 0.0f, vertices, &size);

    return size;
}

void CFont::ReleaseVertexBuffer()
{
    SafeRelease(m_vertexBuffer);
    SafeRelease(m_indexBuffer);
    m_vertexCapacity = 0;
    m_vertexOffset = 0;
}

void CFont::ReleaseAtlas()
{
    SafeRelease(m_atlas);
    m_atlasWidth = 0;
    m_atlasHeight = 0;
    m_glyphs = {};
    m_kerning.clear();
}

void CFont::ReleaseFontObject()
{
    if (m_dc && m_oldFont && m_oldFont != HGDI_ERROR)
    {
        SelectObject(m_dc, m_oldFont);
        m_oldFont = nullptr;
    }

    if (m_font)
    {
        DeleteObject(m_font);
        m_font = nullptr;
    }

    if (m_dc)
    {
        DeleteDC(m_dc);
        m_dc = nullptr;
    }
}

CFonts::CFonts(IDirect3DDevice9* device, const CConfig* config) : m_device(device), m_config(config)
{
    if (m_device)
        m_device->AddRef();

    Reset();
}

CFonts::~CFonts()
{
    ReleaseFonts();
    SafeRelease(m_device);
}

void CFonts::OnLostDevice()
{
    if (m_font)
        m_font->OnLostDevice();

    if (m_littleFont)
        m_littleFont->OnLostDevice();

    if (m_licensePlateFont)
        m_licensePlateFont->OnLostDevice();
#if defined(_DEBUG)
    if (m_fpsFont)
        m_fpsFont->OnLostDevice();
#endif // _DEBUG
}

void CFonts::OnResetDevice()
{
    if (m_font)
        m_font->OnResetDevice();

    if (m_littleFont)
        m_littleFont->OnResetDevice();

    if (m_licensePlateFont)
        m_licensePlateFont->OnResetDevice();
#if defined(_DEBUG)
    if (m_fpsFont)
        m_fpsFont->OnResetDevice();
#endif // _DEBUG
}

#if defined(SACO_BUILD)
bool CFonts::ReadFontSettings(int& sizeOffset, int& weight, std::string& face) const
{
    sizeOffset = 0;
    weight = 700;
    face = "Arial";

    if (!m_config)
        return false;

    int configuredSize = 0;
    if (m_config->GetParam(CConfig::PARAM_FONT_SIZE, configuredSize))
        sizeOffset = configuredSize;

    int configuredWeight = 0;
    if (m_config->GetParam(CConfig::PARAM_FONT_WEIGHT, configuredWeight))
    {
        // exact SA-MP behavior: only fontweight=1 selects FW_NORMAL.
        weight = configuredWeight == 1 ? 400 : 700;
    }

    std::string configuredFace;
    if (m_config->GetParam(CConfig::PARAM_FONT_FACE, configuredFace) && !configuredFace.empty())
        face = std::move(configuredFace);

    return true;
}
#endif // SACO_BUILD

void CFonts::GetFontSettings(int& sizeOffset, int& weight, std::string& face) const
{
    sizeOffset = 0;
    weight = 700;
    face = "Arial";

#if defined(SACO_BUILD)
    ReadFontSettings(sizeOffset, weight, face);
#endif // SACO_BUILD
}

int CFonts::GetBaseFontSize() const
{
    if (!m_device)
        return 14;

    D3DVIEWPORT9 viewport{};
    if (FAILED(m_device->GetViewport(&viewport)))
        return 14;

    // this is the same resolution-dependent base used by SA-MP R5.
    if (viewport.Width < 1024)
        return 14;

    if (viewport.Width < 1400)
        return 16;

    if (viewport.Width < 1600)
        return 18;

    return 20;
}

bool CFonts::Reset()
{
    if (!m_device)
        return false;

    ReleaseFonts();

    int sizeOffset = 0;
    int weight = 700;
    std::string face;
    GetFontSettings(sizeOffset, weight, face);

    // sa-mp adds fontsize * 2 to the resolution-dependent base size.
    const int normalHeight = std::max(1, GetBaseFontSize() + sizeOffset * 2);
    const int littleHeight = std::max(1, normalHeight - 2);

    m_font = new CFont(m_device, normalHeight, 0, weight, face.c_str());
    m_littleFont = new CFont(m_device, littleHeight, 0, weight, face.c_str());
    m_licensePlateFont = new CFont(m_device, 38, 10, 700, "Arial");
#if defined(_DEBUG)
    const int fpsHeight = std::max(1, static_cast<int>(normalHeight * 0.75f + 0.5f));
    m_fpsFont = new CFont(m_device, fpsHeight, 0, weight, face.c_str());
    if (!m_fpsFont->IsValid())
    {
        ReleaseFonts();
        return false;
    }
#endif // _DEBUG

    if (!m_font->IsValid() || !m_littleFont->IsValid() || !m_licensePlateFont->IsValid())
    {
        ReleaseFonts();
        return false;
    }

    const SIZE normal = m_font->GetTextSize("Y", 0);
    const SIZE little = m_littleFont->GetTextSize("Y", 0);

    m_charHeight = normal.cy;
    m_littleCharHeight = little.cy;
    return true;
}

void CFonts::GetTextScreenSize(SIZE* size, const char* text, int format) const
{
    if (!size)
        return;

    *size = {};

    if (!m_font || !text || std::strlen(text) > kMaxTextLength)
        return;

    *size = m_font->GetTextSize(text, static_cast<DWORD>(format));
}

void CFonts::GetLittleTextScreenSize(SIZE* size, const char* text, int format) const
{
    if (!size)
        return;

    *size = {};

    if (!m_littleFont || !text || std::strlen(text) > kMaxTextLength)
        return;

    *size = m_littleFont->GetTextSize(text, static_cast<DWORD>(format));
}

void CFonts::DrawText(const char* text, RECT rect, D3DCOLOR color, bool shadow)
{
    if (!m_font || !text || !*text)
        return;

    // original CFonts::DrawText uses DT_NOCLIP and a 1 px four-way black outline.
    if (shadow)
    {
        m_font->DrawTextOutlinedA(text, &rect, DT_NOCLIP, color, 0xFF000000u, 1);
    }
    else
    {
        m_font->DrawTextA(text, &rect, DT_NOCLIP, color);
    }
}

void CFonts::DrawLittleText(const char* text, RECT rect, int format, D3DCOLOR color, bool shadow)
{
    if (!m_littleFont || !text || !*text)
        return;

    if (shadow)
    {
        m_littleFont->DrawTextOutlinedA(text, &rect, static_cast<DWORD>(format), color, 0xFF000000u, 1);
    }
    else
    {
        m_littleFont->DrawTextA(text, &rect, static_cast<DWORD>(format), color);
    }
}

void CFonts::DrawLicensePlateText(const char* text, RECT rect, D3DCOLOR color)
{
    if (!m_licensePlateFont || !text || !*text)
        return;

    std::string buffer(text);
    if (buffer.size() > kMaxTextLength)
        return;
    CFont::StripColorTags(buffer);
    constexpr DWORD format = DT_CENTER | DT_VCENTER | DT_NOCLIP;
    constexpr D3DCOLOR outline = 0xB4F0EEE4u;
    const int count = static_cast<int>(buffer.size());
    CFont::Batch batch(m_licensePlateFont);
    // r5 changes just one edge; center alignment moves the glyphs by one pixel
    rect.top -= 2;
    m_licensePlateFont->DrawTextA(buffer.c_str(), count, &rect, format, outline);
    rect.top += 2;
    rect.left += 2;
    m_licensePlateFont->DrawTextA(buffer.c_str(), count, &rect, format, outline);
    rect.left -= 2;
    rect.top += 2;
    m_licensePlateFont->DrawTextA(buffer.c_str(), count, &rect, format, outline);
    rect.top -= 2;
    rect.left -= 2;
    m_licensePlateFont->DrawTextA(buffer.c_str(), count, &rect, format, outline);
    rect.left += 2;
    m_licensePlateFont->DrawTextA(buffer.c_str(), count, &rect, format, color);
}

#if defined(_DEBUG)
void CFonts::DrawFps(unsigned int fps, double frameTimeMs)
{
    if (!m_fpsFont)
        return;
    D3DVIEWPORT9 viewport{};
    if (FAILED(m_device->GetViewport(&viewport)))
        return;
    char text[64]{};
    std::snprintf(text, sizeof(text), "FPS %u\nMS %.2f", fps, frameTimeMs);
    const SIZE size = m_fpsFont->GetTextSize(text, DT_NOCLIP);
    const LONG right = static_cast<LONG>(viewport.X + viewport.Width) - 4;
    const LONG top = static_cast<LONG>(viewport.Y) + 2;
    RECT rect{right - size.cx, top, right, top + size.cy};
    m_fpsFont->DrawTextA(text, &rect, DT_NOCLIP, 0xC8FFD23Fu);
}
#endif // _DEBUG

void CFonts::ReleaseFonts()
{
    delete m_font;
    m_font = nullptr;

    delete m_littleFont;
    m_littleFont = nullptr;

    delete m_licensePlateFont;
    m_licensePlateFont = nullptr;
#if defined(_DEBUG)
    delete m_fpsFont;
    m_fpsFont = nullptr;
#endif // _DEBUG

    m_charHeight = 0;
    m_littleCharHeight = 0;
}
