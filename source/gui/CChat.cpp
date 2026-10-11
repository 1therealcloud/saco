#include <stdafx.h>
#include "game/CGame.h"

#include "CChat.h"

#include "config/CConfig.h"
#include "render/CFonts.h"
#include "CGui.h"

#include <ctime>

namespace
{
    std::unordered_map<const CChat*, ui::Appearance> g_appearances;
    constexpr long kChatLeft = 45;
    constexpr long kChatTop = 10;
    constexpr long kChatRight = 550;
    constexpr long kChatBottom = 110;
    constexpr long kPrefixGap = 5;

    void CopyCString(char* destination, std::size_t destinationSize, const char* source)
    {
        if (!destination || destinationSize == 0)
            return;

        destination[0] = '\0';
        if (!source)
            return;

        std::strncpy(destination, source, destinationSize - 1);
        destination[destinationSize - 1] = '\0';
    }
}

CChat::CChat(IDirect3DDevice9* device, CFonts* fontRenderer, const char* logPath)
{
    Initialize(device, fontRenderer, logPath, nullptr);
}

CChat::CChat(IDirect3DDevice9* device, CFonts* fontRenderer, const char* logPath, const CConfig* config)
{
    Initialize(device, fontRenderer, logPath, config);
}

CChat::~CChat()
{
    g_appearances.erase(this);
    OnLostDevice();
}

void CChat::Initialize(IDirect3DDevice9* device, CFonts* fontRenderer, const char* logPath, const CConfig* config)
{
    // Match the R5 object state. The original creates D3DX sprites here; this
    // replacement keeps those ABI slots null and renders through CFonts.
    std::memset(this, 0, sizeof(*this));

    m_pFontRenderer = fontRenderer;
    m_pDevice = device;
    m_nMode = DISPLAY_MODE_NORMAL;
    m_textColor = 0xFFFFFFFFu;
    m_infoColor = 0xFF88AA62u;
    m_debugColor = 0xFFA9C4E4u;
    m_nPageSize = 10;
    m_bTimestamps = false;
    m_nScrollbarPos = 1;
    m_bRedraw = TRUE;

    if (logPath && *logPath)
    {
        // R5 receives the complete path to chatlog.txt; it does not append the
        // filename itself. Its buffer is MAX_PATH + 1 bytes and is pre-zeroed.
        std::strncpy(m_szLogPath, logPath, MAX_PATH);
        m_szLogPath[MAX_PATH] = '\0';

        FILE* file = nullptr;
        if (fopen_s(&file, m_szLogPath, "w") == 0 && file)
        {
            m_bDoesLogExist = TRUE;
            std::fclose(file);
        }
    }

    if (m_pDevice)
        m_pDevice->GetDisplayMode(0, &m_displayMode);

    ApplyConfig(config);
    OnResetDevice();
}

int CChat::GetMode() const
{
    return m_nMode;
}

void CChat::SwitchMode()
{
    m_bRedraw = TRUE;

    switch (m_nMode)
    {
        case DISPLAY_MODE_OFF:
            m_nMode = DISPLAY_MODE_NORMAL;
            break;
        case DISPLAY_MODE_NOSHADOW:
            m_nMode = DISPLAY_MODE_OFF;
            break;
        case DISPLAY_MODE_NORMAL:
            m_nMode = DISPLAY_MODE_NOSHADOW;
            break;
        default:
            break;
    }
    g_appearances[this].SetVisible(m_nMode != DISPLAY_MODE_OFF);
}

void CChat::RecalcFontSize()
{
    m_nCharHeight = 0;
    m_nTimestampWidth = 0;

    if (!m_pFontRenderer)
        return;

    SIZE size{};
    m_pFontRenderer->GetTextScreenSize(&size, "Y", DT_CALCRECT | DT_SINGLELINE | DT_LEFT);
    m_nCharHeight = size.cy;

    m_pFontRenderer->GetTextScreenSize(&size, "[19:58:34]", DT_CALCRECT | DT_SINGLELINE | DT_LEFT);
    m_nTimestampWidth = size.cx;
    SyncScrollbar();
}

void CChat::OnLostDevice()
{
    // Original R5 releases render-to-surface resources here. This build has no
    // chat-owned D3DX resources, but keeps the same state transitions.
    if (m_pSurface)
    {
        m_pSurface->Release();
        m_pSurface = nullptr;
    }
    if (m_pTexture)
    {
        m_pTexture->Release();
        m_pTexture = nullptr;
    }

    m_pRenderToSurface = nullptr;
    m_pTextSprite = nullptr;
    m_pSprite = nullptr;
    m_bRenderToSurface = FALSE;
    m_bRedraw = TRUE;
}

void CChat::OnResetDevice()
{
    m_bRenderToSurface = FALSE;

    if (m_pDevice)
        m_pDevice->GetDisplayMode(0, &m_displayMode);

    RecalcFontSize();
    m_bRedraw = TRUE;
}

long CChat::ClampScrollPosition(long position, unsigned int pageSize)
{
    const long maxPosition = std::max<long>(1, 100 - static_cast<long>(pageSize));
    return std::clamp(position, 1L, maxPosition);
}

void CChat::UpdateScrollbar()
{
    // DXUT R5 uses range 1..100 and page size m_nPageSize, then selects
    // 100-pageSize as the first visible slot at the bottom.
    m_nScrollbarPos = ClampScrollPosition(100 - static_cast<long>(m_nPageSize), m_nPageSize);
    SyncScrollbar();
}

void CChat::SyncScrollbar()
{
    if (!m_pScrollbar)
        return;
    m_pScrollbar->SetRange(1, MAX_MESSAGES);
    m_pScrollbar->SetPageSize(m_nPageSize);
    m_pScrollbar->SetTrackPos(m_nScrollbarPos);
    // R5 CChat::UpdateScrollbar at samp.dll+0x67200.
    m_pScrollbar->SetLocation(10, 40);
    m_pScrollbar->SetSize(20, std::max<long>(40, (m_nCharHeight + 1) * m_nPageSize - 60));
}

void CChat::UpdateScrollbarVisibility(bool inputActive)
{
    if (!m_pScrollbar)
        return;
    // R5 CChat::Render (+0x67E00): visible while typing or away from the last page.
    m_pScrollbar->SetVisible(m_nMode != DISPLAY_MODE_OFF && (inputActive || m_pScrollbar->GetTrackPos() != MAX_MESSAGES - static_cast<int>(m_nPageSize)));
}

void CChat::SetPageSize(int value)
{
    if (value < 10 || value > 100)
        return;

    m_nPageSize = static_cast<unsigned int>(value);
    UpdateScrollbar();
    m_bRedraw = TRUE;
}

void CChat::PageUp()
{
    if (m_pScrollbar)
        m_nScrollbarPos = m_pScrollbar->GetTrackPos();
    if (m_nMode == DISPLAY_MODE_OFF)
        return;

    long position = m_nScrollbarPos - static_cast<long>(m_nPageSize);
    if (position < 1)
        position = 1;

    m_nScrollbarPos = ClampScrollPosition(position, m_nPageSize);
    SyncScrollbar();
    m_bRedraw = TRUE;
}

void CChat::PageDown()
{
    if (m_pScrollbar)
        m_nScrollbarPos = m_pScrollbar->GetTrackPos();
    if (m_nMode == DISPLAY_MODE_OFF)
        return;

    long position = m_nScrollbarPos == 1 ? static_cast<long>(m_nPageSize) : m_nScrollbarPos + static_cast<long>(m_nPageSize);
    if (position > 100)
        position = 100;

    m_nScrollbarPos = ClampScrollPosition(position, m_nPageSize);
    SyncScrollbar();
    m_bRedraw = TRUE;
}

void CChat::ScrollToBottom()
{
    // R5 literally requests 90; the DXUT scrollbar clamps this according to
    // the active page size.
    m_nScrollbarPos = ClampScrollPosition(90, m_nPageSize);
    SyncScrollbar();
    m_bRedraw = TRUE;
}

void CChat::Scroll(int delta)
{
    if (m_pScrollbar)
        m_nScrollbarPos = m_pScrollbar->GetTrackPos();
    if (m_nMode == DISPLAY_MODE_OFF || delta == 0)
        return;

    UINT lines = 3;
    SystemParametersInfoA(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
    if (lines == WHEEL_PAGESCROLL)
        lines = m_nPageSize;

    m_nScrollbarPos = ClampScrollPosition(m_nScrollbarPos - static_cast<long>(delta) * static_cast<long>(lines), m_nPageSize);
    SyncScrollbar();
    m_bRedraw = TRUE;
}

void CChat::SetTimestamps(bool enabled)
{
    m_bTimestamps = enabled;
    m_bRedraw = TRUE;
}

bool CChat::GetTimestamps() const
{
    return m_bTimestamps;
}

void CChat::FilterOutInvalidChars(char* string) const
{
    if (!string)
        return;

    while (*string)
    {
        const signed char value = static_cast<signed char>(*string);
        if (value > 0 && value < 0x20)
            *string = ' ';
        ++string;
    }
}

void CChat::PushBack()
{
    // Exact R5 copy: entry[1..99] -> entry[0..98]. entry[99] is deliberately
    // left untouched and overwritten field-by-field by AddEntry().
    std::memmove(&m_entry[0], &m_entry[1], sizeof(ChatEntry) * (MAX_MESSAGES - 1));
}

void CChat::RenderEntry(const char* text, RECT rect, D3DCOLOR color)
{
    if (!m_pFontRenderer || !text || !*text)
        return;

    m_pFontRenderer->DrawText(text, rect, color | 0xFF000000u, m_nMode == DISPLAY_MODE_NORMAL);
}

void CChat::Log(int type, const char* text, const char* prefix)
{
    if (!m_bDoesLogExist || !m_szLogPath[0] || !text)
        return;

    FILE* file = nullptr;
    if (fopen_s(&file, m_szLogPath, "a") != 0 || !file)
        return;

    char timestamp[64]{};
    FormatTimestamp(GetCurrentTimestamp(), timestamp);

    if (type == ENTRY_TYPE_CHAT)
    {
        if (prefix)
            std::fprintf(file, "%s <%s> %s\n", timestamp, prefix, text);
    }
    else if (type == ENTRY_TYPE_INFO || type == ENTRY_TYPE_DEBUG)
    {
        std::fprintf(file, "%s %s\n", timestamp, text);
    }

    std::fclose(file);
}

void CChat::ResetDialogControls(cgui::Dialog* gameUi)
{
    if (m_pGameUi)
        m_pGameUi->RemoveControl(2);
    m_pGameUi = gameUi;
    m_pEditbox = nullptr;
    m_pScrollbar = nullptr;
    if (gameUi)
    {
        m_pScrollbar = gameUi->AddScrollBar(2, 10, 40, 20, 40);
        m_pScrollbar->SetVisible(false);
    }
    UpdateScrollbar();
    m_bRedraw = TRUE;
}

void CChat::Render()
{
    if (m_nMode == DISPLAY_MODE_OFF)
        return;

    // In R5 Draw() chooses cached render-to-surface or direct mode. Our renderer
    // is already direct, so Render() is the equivalent final path.
    Draw();
}

std::size_t CChat::FindSplitPosition(const char* text)
{
    if (!text)
        return CHAT_SPLIT_LENGTH;

    std::size_t split = CHAT_SPLIT_LENGTH;
    if (text[split] == ' ')
        return split;

    while (split != 0 && text[split - 1] != ' ')
        --split;

    if (CHAT_SPLIT_LENGTH - split > CHAT_SPLIT_BACKTRACK)
        return CHAT_SPLIT_LENGTH;

    return split;
}

void CChat::CopyCount(char* destination, const char* source, std::size_t count)
{
    if (!destination || !source || count == 0)
        return;

    std::strncpy(destination, source, count);
}

void CChat::AddEntry(int type, const char* text, const char* prefix, D3DCOLOR textColor, D3DCOLOR prefixColor)
{
    if (!text)
        return;

    if (prefix && std::strlen(prefix) > MAX_PREFIX_LENGTH)
        return;
    PushBack();

    ChatEntry& entry = m_entry[MAX_MESSAGES - 1];
    entry.m_nType = type;
    entry.m_textColor = textColor;
    entry.m_prefixColor = prefixColor;
    entry.m_timestamp = GetCurrentTimestamp();

    if (prefix)
    {
        std::strcpy(entry.m_szPrefix, prefix);
        std::strcat(entry.m_szPrefix, ":");
    }
    else
    {
        entry.m_szPrefix[0] = '\0';
    }

    Log(type, text, prefix);

    if (type == ENTRY_TYPE_CHAT && std::strlen(text) > CHAT_SPLIT_LENGTH)
    {
        const std::size_t split = FindSplitPosition(text);
        const bool splitOnSpace = split != CHAT_SPLIT_LENGTH || text[CHAT_SPLIT_LENGTH] == ' ';

        if (!splitOnSpace)
        {
            CopyCount(entry.m_szText, text, CHAT_SPLIT_LENGTH);
            entry.m_szText[CHAT_SPLIT_LENGTH] = '\0';

            PushBack();

            ChatEntry& continuation = m_entry[MAX_MESSAGES - 1];
            continuation.m_nType = type;
            continuation.m_textColor = textColor;
            continuation.m_prefixColor = prefixColor;
            continuation.m_szPrefix[0] = '\0';
            CopyCString(continuation.m_szText, sizeof(continuation.m_szText), text + CHAT_SPLIT_LENGTH);
        }
        else
        {
            CopyCount(entry.m_szText, text, split);
            entry.m_szText[split] = '\0';

            PushBack();

            ChatEntry& continuation = m_entry[MAX_MESSAGES - 1];
            continuation.m_nType = type;
            continuation.m_textColor = textColor;
            continuation.m_prefixColor = prefixColor;
            continuation.m_szPrefix[0] = '\0';
            CopyCString(continuation.m_szText, sizeof(continuation.m_szText), text + split + 1);
        }

        m_bRedraw = TRUE;
        return;
    }

    // R5 copies 144 bytes and writes the terminator at text[144], which is the
    // first byte of the 64-byte unused area. Preserve that quirk.
    CopyCount(entry.m_szText, text, MAX_TEXT_LENGTH);
    entry.unused[0] = '\0';
    m_bRedraw = TRUE;
}

void CChat::DrawEntry(const ChatEntry& entry, long& left, long top, long bottom)
{
    if (entry.m_nType == ENTRY_TYPE_NONE)
        return;

    RECT rect{left, top, kChatRight, bottom};

    // R5 permits all 144 text bytes and terminates in unused[0]. Give CFonts
    // a separate terminated string rather than reading past the text array.
    char text[MAX_TEXT_LENGTH + 1]{};
    std::memcpy(text, entry.m_szText, MAX_TEXT_LENGTH);

    char timestamp[64]{};
    if (m_bTimestamps && entry.m_timestamp)
        FormatTimestamp(entry.m_timestamp, timestamp);

    if (entry.m_nType == ENTRY_TYPE_INFO || entry.m_nType == ENTRY_TYPE_DEBUG)
    {
        if (m_bTimestamps && timestamp[0])
        {
            RenderEntry(timestamp, rect, entry.m_textColor);
            left = kChatLeft + m_nTimestampWidth + kPrefixGap;
            rect.left = left;
        }

        RenderEntry(text, rect, entry.m_textColor);
        return;
    }

    if (entry.m_nType != ENTRY_TYPE_CHAT)
        return;

    if (m_bTimestamps && timestamp[0] && entry.m_szPrefix[0])
    {
        RenderEntry(timestamp, rect, entry.m_textColor);
        left = kChatLeft + m_nTimestampWidth + kPrefixGap;
        rect.left = left;
    }

    if (entry.m_szPrefix[0])
    {
        SIZE size{};
        m_pFontRenderer->GetTextScreenSize(&size, entry.m_szPrefix, DT_CALCRECT | DT_SINGLELINE | DT_LEFT);
        RenderEntry(entry.m_szPrefix, rect, entry.m_prefixColor);
        left += size.cx + kPrefixGap;
        rect.left = left;
    }

    RenderEntry(text, rect, entry.m_textColor);
}

void CChat::Draw()
{
    auto& appearance = g_appearances[this];
    appearance.SetVisible(m_nMode != DISPLAY_MODE_OFF);
    ui::OpacityScope fade(appearance.GetOpacity());
    if (m_pScrollbar)
    {
        m_nScrollbarPos = m_pScrollbar->GetTrackPos();
    }
    if (m_nMode == DISPLAY_MODE_OFF || !m_pFontRenderer || m_nPageSize == 0)
        return;

    CFont::Batch batch(m_pFontRenderer->GetFont());

    long position = m_nScrollbarPos;
    if (position < 1)
        position = 1;

    long top = kChatTop;
    long bottom = kChatBottom;

    for (unsigned int line = 0; line < m_nPageSize; ++line)
    {
        const long index = position + static_cast<long>(line);
        if (index < 0 || index >= MAX_MESSAGES)
            break;

        long left = kChatLeft;
        DrawEntry(m_entry[index], left, top, bottom);

        const long nextTop = top + m_nCharHeight + 1;
        bottom = nextTop + m_nCharHeight + 1;
        top = nextTop;
    }

    m_nWindowBottom = bottom;
}

void CChat::RenderToSurface()
{
    // Behavioural replacement for the R5 cache: redraw only when dirty or when
    // the logical scrollbar changed. Drawing itself stays on our direct font path.
    if (m_nMode == DISPLAY_MODE_OFF)
        return;

    // No cached texture in our renderer: redraw on every frame.
    Draw();
    m_bRedraw = FALSE;
    m_pad[1] = m_nScrollbarPos;
}

void CChat::AddChatMessage(const char* prefix, D3DCOLOR prefixColor, char* text)
{
    if (!text)
        return;

    FilterOutInvalidChars(text);
    AddEntry(ENTRY_TYPE_CHAT, text, prefix, m_textColor, prefixColor);
}

void CChat::AddChatMessage(const char* prefix, D3DCOLOR prefixColor, const char* text)
{
    if (!text)
        return;

    // Convenience overload for literals. The original API takes a writable text
    // buffer and sanitizes it in place.
    char buffer[512]{};
    CopyCString(buffer, sizeof(buffer), text);
    AddChatMessage(prefix, prefixColor, buffer);
}

void CChat::ExpandControlKeyTokens(char* text, std::size_t capacity)
{
    if (!text || capacity == 0 || !std::strstr(text, "~k~"))
        return;

    // R5 helper 0xB6100 does this only for strings shorter than 400 bytes,
    // expands GTA control-key tokens through gta_sa.exe 0x69E160, then copies
    // the result back only when it fits the caller supplied capacity.
    if (std::strlen(text) >= 400)
        return;

    char expanded[2052]{};
    CopyCString(expanded, sizeof(expanded), text);

    CGame::ExpandControlKeys(expanded);

    const std::size_t length = std::strlen(expanded);
    if (length < capacity)
        std::strcpy(text, expanded);
}

void CChat::AddMessage(D3DCOLOR color, char* text)
{
    if (!text)
        return;

    if (std::strlen(text) > MAX_TEXT_LENGTH)
        return;

    FilterOutInvalidChars(text);
    char buffer[0x100]{};
    CopyCString(buffer, sizeof(buffer), text);
    ExpandControlKeyTokens(buffer, sizeof(buffer));
    FilterOutInvalidChars(buffer);
    color = (color >> 8) | 0xFF000000u;
    AddEntry(ENTRY_TYPE_INFO, buffer, nullptr, color, 0);
}

void CChat::AddMessage(D3DCOLOR color, const char* text)
{
    if (!text)
        return;

    char buffer[0x100]{};
    CopyCString(buffer, sizeof(buffer), text);
    AddMessage(color, buffer);
}

void CChat::PrintV(int type, D3DCOLOR color, const char* format, va_list args)
{
    if (!format)
        return;

    char buffer[512]{};
    if (vsnprintf_s(buffer, sizeof(buffer), _TRUNCATE, format, args) < 0 && !buffer[0])
        return;
    FilterOutInvalidChars(buffer);
    AddEntry(type, buffer, nullptr, color, 0);
}

void CChat::PrintInfo(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    PrintV(ENTRY_TYPE_INFO, m_infoColor, format, args);
    va_end(args);
}

void CChat::PrintDebug(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    PrintV(ENTRY_TYPE_DEBUG, m_debugColor, format, args);
    va_end(args);
}

const CChat::ChatEntry* CChat::GetEntries() const
{
    return m_entry;
}

std::size_t CChat::GetEntryCount() const
{
    // R5 keeps a fixed 100-slot history and has no count field.
    std::size_t count = 0;
    for (const ChatEntry& entry : m_entry)
    {
        if (entry.m_nType != ENTRY_TYPE_NONE)
            ++count;
    }
    return count;
}

void CChat::ApplyConfig(const CConfig* config)
{
    if (!config)
        return;

    int pageSize = 0;
    if (config->GetParam(CConfig::PARAM_PAGE_SIZE, pageSize))
        SetPageSize(pageSize);

    bool timestamps = false;
    if (config->GetParam(CConfig::PARAM_TIMESTAMP, timestamps))
        m_bTimestamps = timestamps;

    // the chat currently always uses direct drawing
    // bool directMode = false;
    // if (config->GetParam(CConfig::PARAM_DIRECT_MODE, directMode) && directMode)
    //     m_bRenderToSurface = FALSE;
}

std::int32_t CChat::GetCurrentTimestamp()
{
    return static_cast<std::int32_t>(std::time(nullptr));
}

void CChat::FormatTimestamp(std::int32_t timestamp, char (&buffer)[64])
{
    buffer[0] = '\0';

    const std::time_t value = static_cast<std::time_t>(timestamp);
    std::tm local{};
    if (localtime_s(&local, &value) != 0)
        return;

    std::strftime(buffer, sizeof(buffer), "[%H:%M:%S]", &local);
}
