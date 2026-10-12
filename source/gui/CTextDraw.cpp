#include <stdafx.h>

#include "game/CGame.h"

#include "CTextDraw.h"
#include "game/render/model_preview.h"

#include "game/render/textures.h"
#include "game/RenderWare/RenderWare.h"

#include <cmath>
#include <cstdio>

namespace
{
    unsigned int g_drawDepth = 0;
    int g_screenWidth = 0, g_screenHeight = 0;
    float g_scaleX = 0, g_scaleY = 0;

    template <typename T>
    T GameFunction(std::uintptr_t address)
    {
        return reinterpret_cast<T>(address);
    }
    int ScreenWidth()
    {
        return g_drawDepth ? g_screenWidth : CGame::ScreenWidth();
    }

    int ScreenHeight()
    {
        return g_drawDepth ? g_screenHeight : CGame::ScreenHeight();
    }

    float HudScaleX()
    {
        return g_drawDepth ? g_scaleX : CGame::HudScaleX();
    }

    float HudScaleY()
    {
        return g_drawDepth ? g_scaleY : CGame::HudScaleY();
    }

    std::int32_t FloatToInt(float value)
    {
        ZoneScoped;
        std::int64_t result;
        __asm
        {
        fld value
        fistp result
        }
        return static_cast<std::int32_t>(result);
    }

    void FontSetScale(float x, float y)
    {
        GameFunction<void(__cdecl*)(float, float)>(0x719380)(x, y);
    }

    void FontSetColor(D3DCOLOR color)
    {
        GameFunction<void(__cdecl*)(D3DCOLOR)>(0x719430)(color);
    }

    void FontSetFontStyle(int style)
    {
        GameFunction<void(__cdecl*)(int)>(0x719490)(style);
    }

    void FontSetWrapX(float value)
    {
        GameFunction<void(__cdecl*)(float)>(0x7194D0)(value);
    }

    void FontSetCentreSize(float value)
    {
        GameFunction<void(__cdecl*)(float)>(0x7194E0)(value);
    }

    void FontSetDropColor(D3DCOLOR color)
    {
        GameFunction<void(__cdecl*)(D3DCOLOR)>(0x719510)(color);
    }

    void FontSetDropShadowPosition(std::uint8_t value)
    {
        GameFunction<void(__cdecl*)(std::uint8_t)>(0x719570)(value);
    }

    void FontSetEdge(std::uint8_t value)
    {
        GameFunction<void(__cdecl*)(std::uint8_t)>(0x719590)(value);
    }

    void FontSetProportional(std::uint8_t value)
    {
        GameFunction<void(__cdecl*)(std::uint8_t)>(0x7195B0)(value);
    }

    void FontSetBackground(std::uint8_t enable, std::uint8_t includeWrap)
    {
        GameFunction<void(__cdecl*)(std::uint8_t, std::uint8_t)>(0x7195C0)(enable, includeWrap);
    }

    void FontSetBackgroundColor(D3DCOLOR color)
    {
        GameFunction<void(__cdecl*)(D3DCOLOR)>(0x7195E0)(color);
    }

    void FontSetUnknown(std::uint8_t value)
    {
        GameFunction<void(__cdecl*)(std::uint8_t)>(0x719600)(value);
    }

    void FontSetOrientation(int value)
    {
        GameFunction<void(__cdecl*)(int)>(0x719610)(value);
    }

    void FontPrintString(float x, float y, const char* text)
    {
        GameFunction<void(__cdecl*)(float, float, const char*)>(0x71A700)(x, y, text);
    }

    void InsertPlayerControlKeys(char* text)
    {
        GameFunction<void(__cdecl*)(char*)>(0x69E160)(text);
    }

    struct Sprite2d
    {
        RwTexture* texture;
    };

    struct Rgba
    {
        std::uint8_t r;
        std::uint8_t g;
        std::uint8_t b;
        std::uint8_t a;
    };

    struct Rect
    {
        float left;
        float bottom;
        float right;
        float top;
    };

    void DrawSprite(RwTexture* texture, const Rect& rect, D3DCOLOR color)
    {
        ZoneScoped;
        if (!texture)
            return;

        auto* renderWare = *reinterpret_cast<std::uintptr_t**>(0x00C97B24);
        reinterpret_cast<void(__cdecl*)(int, int)>(renderWare[8])(9, 2);

        Sprite2d sprite{texture};

        // SA-MP passes the four stored color bytes directly as CRGBA.
        Rgba rgba{};
        static_assert(sizeof(rgba) == sizeof(color));
        std::memcpy(&rgba, &color, sizeof(rgba));

        GameFunction<void(__thiscall*)(Sprite2d*, const Rect&, const Rgba&)>(0x728350)(&sprite, rect, rgba);
    }

    constexpr std::size_t MAX_TEXT_LENGTH = 800;
    constexpr std::size_t MAX_KEY_TEXT_LENGTH = 255;
    constexpr std::uint32_t INVALID_RESOURCE = 0xFFFFFFFFu;
    constexpr std::size_t RESOURCE_COUNT = 200;

    constexpr float DEFAULT_SCREEN_WIDTH = 640.0f;
    constexpr float DEFAULT_SCREEN_HEIGHT = 448.0f;

    constexpr const char* ERROR_K_CODE = "Error: can't use k-codes in long string";
    constexpr const char* ERROR_TILDE = "Error: unmatched tilde";

    std::array<game::render::textures::Texture, RESOURCE_COUNT> g_resources{};
    std::array<bool, RESOURCE_COUNT> g_resourceUsed{};

    std::uint32_t AcquireResource()
    {
        ZoneScoped;
        for (std::uint32_t i = 0; i < RESOURCE_COUNT; ++i)
        {
            if (g_resourceUsed[i])
                continue;

            g_resourceUsed[i] = true;
            g_resources[i] = nullptr;
            return i;
        }

        return INVALID_RESOURCE;
    }

    void ReleaseResourceSlot(std::uint32_t index)
    {
        if (index >= RESOURCE_COUNT)
            return;

        g_resourceUsed[index] = false;
        g_resources[index].reset();
    }

    game::render::textures::TextureHandle* GetResource(std::uint32_t index)
    {
        if (index >= RESOURCE_COUNT || !g_resourceUsed[index])
            return nullptr;

        return g_resources[index].get();
    }

    void SetResource(std::uint32_t index, game::render::textures::Texture texture)
    {
        if (index >= RESOURCE_COUNT || !g_resourceUsed[index])
            return;

        g_resources[index] = std::move(texture);
    }

    bool ValidateTildes(const char* text, bool& containsKeys)
    {
        ZoneScoped;
        bool inside = false;
        std::int32_t length = 0;
        bool valid = true;
        containsKeys = false;

        for (const char* p = text; *p; ++p)
        {
            if (*p == '~')
            {
                if (inside)
                {
                    length = 0;
                    inside = false;
                    continue;
                }

                inside = true;
                if (p[1] == 'k' || p[1] == 'K')
                    containsKeys = true;
            }

            if (inside)
            {
                ++length;
                if (length > 24)
                    valid = false;
            }
        }

        return valid && !inside;
    }

    bool SplitTextureName(const char* text, char (&txd)[65], char (&texture)[65])
    {
        ZoneScoped;
        txd[0] = '\0';
        texture[0] = '\0';

        const char* separator = std::strchr(text, ':');
        if (!separator || std::strlen(text) >= 64 || std::strchr(text, '\\') || std::strchr(text, '/'))
            return false;

        const std::size_t txdLength = static_cast<std::size_t>(separator - text);
        std::memcpy(txd, text, txdLength);
        txd[txdLength] = '\0';
        std::strcpy(texture, separator + 1);
        return true;
    }

    game::render::textures::Texture ReadTextureFromSlot(int slot, const char* texture)
    {
        if (slot < 0)
            return {};

        return game::render::textures::read(slot, texture);
    }

    game::render::textures::Texture ReadTextureFromDictionary(const char* dictionary, const char* texture)
    {
        return ReadTextureFromSlot(game::render::textures::find_slot(dictionary), texture);
    }

    game::render::textures::Texture EnsureDictionaryAndRead(const char* dictionary, const char* path, const char* texture)
    {
        int slot = game::render::textures::find_slot(dictionary);
        if (slot < 0)
            slot = game::render::textures::load_dictionary(dictionary, path);

        return ReadTextureFromSlot(slot, texture);
    }

    game::render::textures::Texture LoadTextureByName(const char* text)
    {
        ZoneScoped;
        char txd[65]{};
        char texture[65]{};
        if (!SplitTextureName(text, txd, texture))
            return {};

        // R5 handles these names before the generic models\\txd path.
        if (std::strcmp(txd, "hud") == 0)
            return ReadTextureFromDictionary("hud", texture);

        if (std::strcmp(txd, "samaps") == 0)
            return EnsureDictionaryAndRead("samaps", "SAMP\\samaps.txd", texture);

        if (std::strcmp(txd, "vehicleprev") == 0)
            return EnsureDictionaryAndRead("vehicles128", "SAMP\\vehicles128.txd", texture);

        char path[MAX_PATH]{};
        std::snprintf(path, sizeof(path), "models\\txd\\%s.txd", txd);
        return EnsureDictionaryAndRead(txd, path, texture);
    }

}

CTextDraw::CTextDraw(const Transmit* transmit, const char* text)
{
    ZoneScoped;
    std::memset(&m_data, 0, sizeof(m_data));

    m_data.m_fLetterWidth = transmit->m_fLetterWidth;
    m_data.m_fLetterHeight = transmit->m_fLetterHeight;
    m_data.m_letterColor = transmit->m_letterColor;
    m_data.m_nUnknown = 0;
    m_data.m_bCenter = transmit->m_bCenter;
    m_data.m_bBox = transmit->m_bBox;
    m_data.m_fBoxSizeX = transmit->m_fBoxWidth;
    m_data.m_fBoxSizeY = transmit->m_fBoxHeight;
    m_data.m_boxColor = transmit->m_boxColor;
    m_data.m_bProportional = transmit->m_bProportional;
    m_data.m_backgroundColor = transmit->m_backgroundColor;
    m_data.m_nShadow = transmit->m_nShadow;
    m_data.m_nOutline = transmit->m_bOutline;
    m_data.m_bLeft = transmit->m_bLeft;
    m_data.m_bRight = transmit->m_bRight;
    m_data.m_nStyle = transmit->m_nStyle;
    m_data.m_fX = transmit->m_fX;
    m_data.m_fY = transmit->m_fY;
    m_data.m_nTextureSlot = INVALID_RESOURCE;
    m_data.m_nTextureSlot2 = INVALID_RESOURCE;
    m_data.m_bSelectable = transmit->m_bSelectable;
    m_data.m_nModel = transmit->m_nModel;
    m_data.m_rotation = transmit->m_rotation;
    m_data.m_fZoom = transmit->m_fZoom;
    m_data.m_aColor[0] = transmit->m_aColor[0];
    m_data.m_aColor[1] = transmit->m_aColor[1];
    m_data.m_bTextContainsKeys = 0;
    m_data.m_nIndex = INVALID_RESOURCE;

    SetText(text);

    m_data.m_bDrawnThisFrame = 0;
    m_data.m_nComputedLeft = 0;
    m_data.m_nComputedTop = 0;
    m_data.m_nComputedRight = 0;
    m_data.m_nComputedBottom = 0;
    m_data.m_bSelected = 0;
    m_data.m_selectedColor = 0;
}

CTextDraw::~CTextDraw()
{
    ZoneScoped;
    ReleaseResource();
}

void CTextDraw::SetText(const char* text)
{
    ZoneScoped;
    m_szString[0] = '\0';
    std::memset(m_szText, 0, sizeof(m_szText));
    m_data.m_bTextContainsKeys = 0;
    m_data.m_bDrawnThisFrame = 0;
    if (!text)
        text = "";

    if (std::strlen(text) >= MAX_TEXT_LENGTH)
    {
        if (m_data.m_nStyle == 4)
            ReleaseResource();
        return;
    }

    std::strncpy(m_szText, text, MAX_TEXT_LENGTH);
    m_szText[MAX_TEXT_LENGTH] = '\0';
    m_data.m_bTextContainsKeys = 0;

    bool containsKeys = false;
    if (!ValidateTildes(m_szText, containsKeys))
    {
        std::strcpy(m_szText, ERROR_TILDE);
        containsKeys = false;
    }

    m_data.m_bTextContainsKeys = containsKeys ? 1 : 0;

    if (m_data.m_bTextContainsKeys && std::strlen(m_szText) > MAX_KEY_TEXT_LENGTH)
    {
        std::strcpy(m_szText, ERROR_K_CODE);
        m_data.m_bTextContainsKeys = 0;
    }

    std::strcpy(m_szString, m_szText);
    if (m_data.m_nStyle == 4)
    {
        ReleaseResource();
        m_data.m_nIndex = AcquireResource();
        LoadTexture();
    }
}

void CTextDraw::BeginDrawPass()
{
    ZoneScoped;
    if (!g_drawDepth++)
    {
        g_screenWidth = CGame::ScreenWidth();
        g_screenHeight = CGame::ScreenHeight();
        g_scaleX = CGame::HudScaleX();
        g_scaleY = CGame::HudScaleY();
    }
}
void CTextDraw::EndDrawPass()
{
    ZoneScoped;
    if (g_drawDepth)
        --g_drawDepth;
}

void CTextDraw::Draw()
{
    ZoneScoped;
    m_data.m_bDrawnThisFrame = 0;
    // native fonts and sprites require an active RenderWare camera
    if (!CGame::HasFontResources())
        return;
    if (m_data.m_nStyle >= 4)
    {
        if (m_data.m_nIndex != INVALID_RESOURCE && GetResource(m_data.m_nIndex))
            DrawTexture();
    }
    else
        DrawText();
}

void CTextDraw::DrawTexture()
{
    ZoneScoped;
    const float sx = static_cast<float>(ScreenWidth()) * HudScaleX();
    const float sy = static_cast<float>(ScreenHeight()) * HudScaleY();

    const float left = m_data.m_fX * sx;
    const float top = m_data.m_fY * sy;
    const float right = (m_data.m_fX + m_data.m_fBoxSizeX) * sx;
    const float bottom = (m_data.m_fY + m_data.m_fBoxSizeY) * sy;

    const Rect rect{left, bottom, right, top};
    const D3DCOLOR color = m_data.m_bSelected ? m_data.m_selectedColor : m_data.m_letterColor;
    DrawSprite(GetResource(m_data.m_nIndex), rect, color);

    // R5 rounds the origin first, then adds the scaled size as a float.
    const std::int32_t computedLeft = FloatToInt(left);
    const std::int32_t computedRight = FloatToInt(m_data.m_fBoxSizeX * sx + static_cast<float>(computedLeft));
    const std::int32_t computedTop = FloatToInt(top);
    const std::int32_t computedBottom = FloatToInt(m_data.m_fBoxSizeY * sy + static_cast<float>(computedTop));
    SetComputedRect(computedLeft, computedTop, computedRight, computedBottom);
}

void CTextDraw::DrawText()
{
    ZoneScoped;
    if (!m_szText[0])
        return;

    if (m_data.m_bTextContainsKeys)
        std::strcpy(m_szString, m_szText);

    const float screenWidth = static_cast<float>(ScreenWidth());
    const float screenHeight = static_cast<float>(ScreenHeight());
    const float scaleX = HudScaleX();
    const float scaleY = HudScaleY();

    const float fontScaleY = screenHeight * m_data.m_fLetterHeight * scaleY * 0.5f;
    const float fontScaleX = screenWidth * m_data.m_fLetterWidth * scaleX;
    FontSetScale(fontScaleX, fontScaleY);

    FontSetColor(m_data.m_bSelected ? m_data.m_selectedColor : m_data.m_letterColor);
    FontSetUnknown(0);

    if (m_data.m_bRight)
        FontSetOrientation(2);
    else if (m_data.m_bCenter)
        FontSetOrientation(0);
    else
        FontSetOrientation(1);

    // Both values use the horizontal HUD scale in R5.
    const float sizeX = screenWidth * m_data.m_fBoxSizeX * scaleX;
    const float sizeY = screenWidth * m_data.m_fBoxSizeY * scaleX;
    FontSetWrapX(sizeX);
    FontSetCentreSize(sizeY);

    FontSetBackground(m_data.m_bBox, 0);
    FontSetBackgroundColor(m_data.m_boxColor);
    FontSetProportional(m_data.m_bProportional);
    FontSetDropColor(m_data.m_backgroundColor);

    if (m_data.m_nOutline)
        FontSetEdge(m_data.m_nOutline);
    else
        FontSetDropShadowPosition(m_data.m_nShadow);

    FontSetFontStyle(m_data.m_nStyle);

    if (m_data.m_bTextContainsKeys)
        InsertPlayerControlKeys(m_szString);

    const float yOffset = DEFAULT_SCREEN_HEIGHT - m_data.m_fY;
    const float y = screenHeight - yOffset * (screenHeight * scaleY);
    const float xOffset = DEFAULT_SCREEN_WIDTH - m_data.m_fX;
    const float x = screenWidth - xOffset * (screenWidth * scaleX);

    FontPrintString(x, y, m_szString);
    FontSetEdge(0);

    std::int32_t left;
    std::int32_t right;
    std::int32_t top;
    std::int32_t bottom;

    if (m_data.m_bRight)
    {
        const float delta = sizeX - x;
        left = FloatToInt(x - delta);
        right = FloatToInt(x);
        top = FloatToInt(y);
        bottom = FloatToInt(y + sizeY);
    }
    else if (m_data.m_bCenter)
    {
        left = FloatToInt(x - sizeY * 0.5f);
        right = left + FloatToInt(sizeY);
        top = FloatToInt(y);
        bottom = FloatToInt(y + sizeX);
    }
    else
    {
        left = FloatToInt(x);
        const float delta = sizeX - x;
        right = FloatToInt(delta + x);
        top = FloatToInt(y);
        bottom = FloatToInt(y + sizeY);
    }

    SetComputedRect(left, top, right, bottom);
}

void CTextDraw::LoadTexture()
{
    ZoneScoped;
    if (m_data.m_nStyle != 4 || m_data.m_nIndex == INVALID_RESOURCE)
        return;

    SetResource(m_data.m_nIndex, LoadTextureByName(m_szText));
}

void CTextDraw::LoadPreview()
{
    ZoneScoped;
    if (m_data.m_nStyle != 5 || m_data.m_nIndex != INVALID_RESOURCE)
        return;

    auto textureResource =
        game::render::model_preview::create(m_data.m_nModel, CVector{m_data.m_rotation.x, m_data.m_rotation.y, m_data.m_rotation.z}, m_data.m_fZoom, m_data.m_backgroundColor,
                                            m_data.m_aColor[0] == 0xFFFF ? -1 : m_data.m_aColor[0], m_data.m_aColor[1] == 0xFFFF ? -1 : m_data.m_aColor[1]);
    void* texture = textureResource.release();
    if (!texture)
        return;

    m_data.m_nIndex = AcquireResource();
    if (m_data.m_nIndex == INVALID_RESOURCE)
    {
        game::render::textures::Texture discarded(static_cast<game::render::textures::TextureHandle*>(texture));
        return;
    }

    SetResource(m_data.m_nIndex, game::render::textures::Texture(reinterpret_cast<game::render::textures::TextureHandle*>(texture)));
}

void CTextDraw::UnloadPreview()
{
    ZoneScoped;
    if (m_data.m_nStyle != 5 || m_data.m_nIndex == INVALID_RESOURCE)
        return;

    ReleaseResource();
}

void CTextDraw::ReleaseResource()
{
    ZoneScoped;
    if (m_data.m_nIndex == INVALID_RESOURCE)
        return;

    ReleaseResourceSlot(m_data.m_nIndex);
    m_data.m_nIndex = INVALID_RESOURCE;
}

RECT CTextDraw::GetComputedRect() const
{
    ZoneScoped;
    return RECT{static_cast<LONG>(m_data.m_nComputedLeft), static_cast<LONG>(m_data.m_nComputedTop), static_cast<LONG>(m_data.m_nComputedRight),
                static_cast<LONG>(m_data.m_nComputedBottom)};
}

void CTextDraw::ResetResourcePool()
{
    ZoneScoped;
    for (std::size_t i = 0; i < RESOURCE_COUNT; ++i)
    {
        g_resources[i].reset();
        g_resourceUsed[i] = false;
    }
}

void CTextDraw::SetComputedRect(std::int32_t left, std::int32_t top, std::int32_t right, std::int32_t bottom)
{
    ZoneScoped;
    m_data.m_nComputedLeft = static_cast<std::uint32_t>(left);
    m_data.m_nComputedTop = static_cast<std::uint32_t>(top);
    m_data.m_nComputedRight = static_cast<std::uint32_t>(right);
    m_data.m_nComputedBottom = static_cast<std::uint32_t>(bottom);
    m_data.m_bDrawnThisFrame = 1;
}
