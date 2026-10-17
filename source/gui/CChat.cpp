/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * The original code is copyright (c) 2026, 1therealcloud.
 */

#include <stdafx.h>
#include "game/CGame.h"

#include "CChat.h"

#include "config/CConfig.h"
#include "render/CFonts.h"
#include "CGui.h"
#include "utils/TextEncoding.inl"

#include <ctime>
#include <atomic>
#include <condition_variable>
#include <memory>
#include <thread>

namespace
{
    class ChatLog
    {
    public:
        explicit ChatLog(std::string path)
            : m_worker([this, path = std::move(path)] {
                  Run(path);
              })
        {}
        ~ChatLog()
        {
            {
                std::lock_guard lock(m_mutex);
                m_stopping = true;
            }
            m_ready.notify_one();
            m_worker.join();
        }

        void Write(std::string line)
        {
            std::lock_guard lock(m_mutex);
            if (m_failed.load(std::memory_order_relaxed))
                return;
            if (m_bytes + line.size() > 4 * 1024 * 1024)
            {
#if defined(_DEBUG)
                if (!m_reportedOverflow)
                    ::Log("[chat] log queue is full");
#endif // _DEBUG
                m_reportedOverflow = true;
                return;
            }
            m_bytes += line.size();
            m_pending.push_back(std::move(line));
            m_ready.notify_one();
        }

    private:
        void Run(const std::string& path)
        {
            FILE* file = nullptr;
            if (fopen_s(&file, path.c_str(), "w") != 0 || !file)
            {
                m_failed.store(true, std::memory_order_relaxed);
#if defined(_DEBUG)
                ::Log("[chat] failed to open log file: %s", path.c_str());
#endif // _DEBUG
                return;
            }
            for (;;)
            {
                std::vector<std::string> lines;
                {
                    std::unique_lock lock(m_mutex);
                    m_ready.wait(lock, [this] {
                        return m_stopping || !m_pending.empty();
                    });
                    lines.swap(m_pending);
                    m_bytes = 0;
                    if (lines.empty() && m_stopping)
                        break;
                }
                ZoneScopedN("Chat log write");
                bool failed = false;
                for (const auto& line : lines)
                    if (std::fwrite(line.data(), 1, line.size(), file) != line.size())
                    {
                        failed = true;
                        break;
                    }
                if (std::fflush(file) != 0)
                    failed = true;
                if (failed)
                {
                    m_failed.store(true, std::memory_order_relaxed);
#if defined(_DEBUG)
                    ::Log("[chat] failed to write log file: %s", path.c_str());
#endif // _DEBUG
                    break;
                }
            }
            std::fclose(file);
        }

        std::mutex m_mutex;
        std::condition_variable m_ready;
        std::vector<std::string> m_pending;
        std::size_t m_bytes = 0;
        bool m_stopping = false;
        bool m_reportedOverflow = false;
        std::atomic<bool> m_failed{false};
        std::thread m_worker;
    };
    std::unordered_map<const CChat*, std::unique_ptr<ChatLog>> g_chatLogs;

    std::unordered_map<const CChat*, ui::Appearance> g_appearances;
    struct LineLayout
    {
        std::array<char, CChat::MAX_TEXT_LENGTH + 1> text{};
        char timestamp[64]{};
        bool dirty = true;
    };
    std::unordered_map<const CChat*, std::array<LineLayout, CChat::MAX_MESSAGES>> g_layouts;
    constexpr long kChatLeft = 45;
    constexpr long kChatTop = 10;
    constexpr long kChatRight = 550;
    constexpr long kChatBottom = 110;
    constexpr long kPrefixGap = 5;

    void CopyCString(char* destination, std::size_t destinationSize, const char* source)
    {
        ZoneScoped;
        if (!destination || destinationSize == 0)
            return;

        destination[0] = '\0';
        if (!source)
            return;

        const auto length = text::Boundary(source, destinationSize - 1);
        std::memcpy(destination, source, length);
        destination[length] = '\0';
    }
}

CChat::CChat(IDirect3DDevice9* device, CFonts* fontRenderer, const char* logPath)
{
    ZoneScoped;
    Initialize(device, fontRenderer, logPath, nullptr);
}

CChat::CChat(IDirect3DDevice9* device, CFonts* fontRenderer, const char* logPath, const CConfig* config)
{
    ZoneScoped;
    Initialize(device, fontRenderer, logPath, config);
}

CChat::~CChat()
{
    g_chatLogs.erase(this);
    g_appearances.erase(this);
    g_layouts.erase(this);
    OnLostDevice();
}

void CChat::Initialize(IDirect3DDevice9* device, CFonts* fontRenderer, const char* logPath, const CConfig* config)
{
    ZoneScoped;
    // Match the R5 object state. The original creates D3DX sprites here; this
    // replacement keeps those ABI slots null and renders through CFonts.
    g_layouts.erase(this);
    g_chatLogs.erase(this);
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

        try
        {
            g_chatLogs.emplace(this, std::make_unique<ChatLog>(m_szLogPath));
            m_bDoesLogExist = TRUE;
        }
        catch (const std::system_error&)
        {
#if defined(_DEBUG)
            ::Log("[chat] failed to start log writer");
#endif // _DEBUG
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
    ZoneScoped;
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
    ZoneScoped;
    m_bRedraw = TRUE;
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
    ZoneScoped;
    if (m_cacheState)
    {
        m_cacheState->Release();
        m_cacheState = nullptr;
    }
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
    m_cacheWidth = m_cacheHeight = 0;
    m_cacheFont = nullptr;
    m_bRedraw = TRUE;
}

void CChat::OnResetDevice()
{
    ZoneScoped;
    m_bRenderToSurface = FALSE;
    m_cacheUnavailable = false;

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
    ZoneScoped;
    // DXUT R5 uses range 1..100 and page size m_nPageSize, then selects
    // 100-pageSize as the first visible slot at the bottom.
    m_nScrollbarPos = ClampScrollPosition(100 - static_cast<long>(m_nPageSize), m_nPageSize);
    SyncScrollbar();
}

void CChat::SyncScrollbar()
{
    ZoneScoped;
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
    ZoneScoped;
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
    ZoneScoped;
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
    ZoneScoped;
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
    ZoneScoped;
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
    ZoneScoped;
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
    ZoneScoped;
    // Exact R5 copy: entry[1..99] -> entry[0..98]. entry[99] is deliberately
    // left untouched and overwritten field-by-field by AddEntry().
    auto& layouts = g_layouts[this];
    for (int i = 0; i < MAX_MESSAGES - 1; ++i)
    {
        layouts[i] = std::move(layouts[i + 1]);
        if (i == MAX_MESSAGES - 2)
            layouts[i].dirty = true;
    }
    layouts.back().dirty = true;
    std::memmove(&m_entry[0], &m_entry[1], sizeof(ChatEntry) * (MAX_MESSAGES - 1));
    m_bRedraw = TRUE;
}

void CChat::RenderEntry(const char* text, RECT rect, D3DCOLOR color)
{
    ZoneScoped;
    if (!m_pFontRenderer || !text || !*text)
        return;

    m_pFontRenderer->DrawText(text, rect, color | 0xFF000000u, m_nMode == DISPLAY_MODE_NORMAL);
}

void CChat::Log(int type, const char* text, const char* prefix)
{
    ZoneScoped;
    if (!m_bDoesLogExist || !m_szLogPath[0] || !text)
        return;

    const auto writer = g_chatLogs.find(this);
    if (writer == g_chatLogs.end())
        return;
    if ((type != ENTRY_TYPE_CHAT && type != ENTRY_TYPE_INFO && type != ENTRY_TYPE_DEBUG) || (type == ENTRY_TYPE_CHAT && !prefix))
        return;

    char timestamp[64]{};
    FormatTimestamp(GetCurrentTimestamp(), timestamp);
    std::string line = timestamp;
    if (type == ENTRY_TYPE_CHAT)
    {
        line += " <";
        line += prefix;
        line += "> ";
    }
    else
        line += ' ';
    line += text;
    line += '\n';
    writer->second->Write(std::move(line));
}

void CChat::ResetDialogControls(cgui::Dialog* gameUi)
{
    ZoneScoped;
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
    ZoneScoped;
    if (m_nMode == DISPLAY_MODE_OFF)
        return;

    Draw();
}

std::size_t CChat::FindSplitPosition(const char* value)
{
    if (!value)
        return 0;
    const auto limit = text::Prefix(value, CHAT_SPLIT_LENGTH);
    auto split = limit;
    while (split && value[split] != ' ')
        split = text::Boundary(value, split - 1);
    return text::Count(std::string_view(value + split, limit - split)) > CHAT_SPLIT_BACKTRACK ? limit : split;
}

void CChat::CopyCount(char* destination, const char* source, std::size_t count)
{
    if (!destination || !source || count == 0)
        return;

    const auto length = text::Boundary(source, count);
    std::memcpy(destination, source, length);
    std::memset(destination + length, 0, count - length);
}

void CChat::AddEntry(int type, const char* text, const char* prefix, D3DCOLOR textColor, D3DCOLOR prefixColor)
{
    ZoneScoped;
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

    if (type == ENTRY_TYPE_CHAT && text::Count(text) > CHAT_SPLIT_LENGTH)
    {
        const std::size_t split = FindSplitPosition(text);
        const bool splitOnSpace = text[split] == ' ';

        if (!splitOnSpace)
        {
            CopyCount(entry.m_szText, text, split);
            entry.m_szText[split] = '\0';

            PushBack();

            ChatEntry& continuation = m_entry[MAX_MESSAGES - 1];
            continuation.m_nType = type;
            continuation.m_textColor = textColor;
            continuation.m_prefixColor = prefixColor;
            continuation.m_szPrefix[0] = '\0';
            CopyCString(continuation.m_szText, sizeof(continuation.m_szText), text + split);
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

    CopyCString(entry.m_szText, sizeof(entry.m_szText), text);
    m_bRedraw = TRUE;
}

void CChat::DrawEntry(const ChatEntry& entry, long& left, long top, long bottom)
{
    ZoneScoped;
    if (entry.m_nType == ENTRY_TYPE_NONE)
        return;

    RECT rect{left, top, kChatRight, bottom};

    auto& layout = g_layouts[this][static_cast<std::size_t>(&entry - m_entry)];
    if (layout.dirty)
    {
        std::memcpy(layout.text.data(), entry.m_szText, MAX_TEXT_LENGTH);
        layout.text.back() = '\0';
        std::memset(layout.timestamp, 0, sizeof(layout.timestamp));
        if (entry.m_timestamp)
            FormatTimestamp(entry.m_timestamp, layout.timestamp);
        layout.dirty = false;
    }
    const char* text = layout.text.data();
    const char* timestamp = layout.timestamp;

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
    ZoneScoped;

    auto& appearance = g_appearances[this];
    appearance.SetVisible(m_nMode != DISPLAY_MODE_OFF);
    ui::OpacityScope fade(appearance.GetOpacity());
    if (m_pScrollbar)
    {
        m_nScrollbarPos = m_pScrollbar->GetTrackPos();
    }
    if (m_nMode == DISPLAY_MODE_OFF || !m_pFontRenderer || m_nPageSize == 0)
        return;

    if (!DrawCached())
        DrawMessages(false);
}

void CChat::DrawMessages(bool renderToTexture)
{
    ZoneScoped;
    CFont::Batch batch(m_pFontRenderer->GetFont(), renderToTexture);

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

bool CChat::DrawCached()
{
    ZoneScoped;
    if (m_cacheUnavailable || !m_pDevice || !m_pFontRenderer->GetFont() || !m_pFontRenderer->GetFont()->IsValid())
        return false;

    D3DVIEWPORT9 viewport{};
    if (FAILED(m_pDevice->GetViewport(&viewport)) || !viewport.Width || !viewport.Height || viewport.X || viewport.Y)
        return false;
    const UINT height = std::min<UINT>(viewport.Height, kChatTop + (m_nCharHeight + 1) * m_nPageSize + 4);
    if (m_cacheWidth != viewport.Width || m_cacheHeight != height)
    {
        OnLostDevice();
        D3DCAPS9 caps{};
        if (FAILED(m_pDevice->GetDeviceCaps(&caps)) || !(caps.PrimitiveMiscCaps & D3DPMISCCAPS_SEPARATEALPHABLEND))
        {
            m_cacheUnavailable = true;
            return false;
        }
        if (FAILED(m_pDevice->CreateTexture(viewport.Width, height, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &m_pTexture, nullptr)))
        {
            m_cacheUnavailable = true;
            return false;
        }
        if (FAILED(m_pTexture->GetSurfaceLevel(0, &m_pSurface)))
        {
            OnLostDevice();
            m_cacheUnavailable = true;
            return false;
        }
        m_cacheWidth = viewport.Width;
        m_cacheHeight = height;
        m_bRenderToSurface = TRUE;
    }
    if (!m_cacheState && FAILED(m_pDevice->CreateStateBlock(D3DSBT_ALL, &m_cacheState)))
        return false;
    if (FAILED(m_cacheState->Capture()))
        return false;

    if (m_bRedraw || m_pad[1] != m_nScrollbarPos || m_cacheMode != m_nMode || m_cachePageSize != m_nPageSize || m_cacheTimestamps != m_bTimestamps ||
        m_cacheFont != m_pFontRenderer->GetFont())
    {
        ZoneScopedN("Chat cache: redraw");
        IDirect3DSurface9* target = nullptr;
        IDirect3DSurface9* depth = nullptr;
        if (FAILED(m_pDevice->GetRenderTarget(0, &target)))
            return false;
        const HRESULT depthResult = m_pDevice->GetDepthStencilSurface(&depth);
        if (FAILED(depthResult) && depthResult != D3DERR_NOTFOUND)
        {
            target->Release();
            return false;
        }
        // Text is cached at full opacity with premultiplied RGB and coverage alpha.
        const float opacity = ui::opacity;
        ui::opacity = 1.0f;
        D3DVIEWPORT9 cacheViewport{0, 0, m_cacheWidth, m_cacheHeight, 0.0f, 1.0f};
        bool rendered = SUCCEEDED(m_pDevice->SetDepthStencilSurface(nullptr)) && SUCCEEDED(m_pDevice->SetRenderTarget(0, m_pSurface)) &&
                        SUCCEEDED(m_pDevice->SetViewport(&cacheViewport)) && SUCCEEDED(m_pDevice->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1.0f, 0));
        if (rendered)
            DrawMessages(true);
        ui::opacity = opacity;
        const HRESULT restoreTarget = m_pDevice->SetRenderTarget(0, target);
        const HRESULT restoreDepth = m_pDevice->SetDepthStencilSurface(depth);
        target->Release();
        if (depth)
            depth->Release();
        const HRESULT restoreState = m_cacheState->Apply();
        if (!rendered || FAILED(restoreTarget) || FAILED(restoreDepth) || FAILED(restoreState) || FAILED(m_pDevice->SetViewport(&viewport)))
            return false;
        m_bRedraw = FALSE;
        m_pad[1] = m_nScrollbarPos;
        m_cacheMode = m_nMode;
        m_cachePageSize = m_nPageSize;
        m_cacheTimestamps = m_bTimestamps;
        m_cacheFont = m_pFontRenderer->GetFont();
    }

    struct Vertex
    {
        float x, y, z, rhw;
        D3DCOLOR color;
        float u, v;
    };
    const auto alpha = static_cast<unsigned int>(std::clamp(ui::opacity, 0.0f, 1.0f) * 255.0f + 0.5f);
    const D3DCOLOR color = D3DCOLOR_ARGB(alpha, alpha, alpha, alpha);
    const float right = static_cast<float>(m_cacheWidth) - 0.5f;
    const float bottom = static_cast<float>(m_cacheHeight) - 0.5f;
    const Vertex vertices[] = {{-0.5f, -0.5f, 0, 1, color, 0, 0}, {right, -0.5f, 0, 1, color, 1, 0}, {-0.5f, bottom, 0, 1, color, 0, 1}, {right, bottom, 0, 1, color, 1, 1}};
    m_pDevice->SetVertexShader(nullptr);
    m_pDevice->SetPixelShader(nullptr);
    m_pDevice->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1);
    m_pDevice->SetTexture(0, m_pTexture);
    m_pDevice->SetTexture(1, nullptr);
    m_pDevice->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
    m_pDevice->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    m_pDevice->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    m_pDevice->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    m_pDevice->SetRenderState(D3DRS_LIGHTING, FALSE);
    m_pDevice->SetRenderState(D3DRS_FOGENABLE, FALSE);
    m_pDevice->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    m_pDevice->SetRenderState(D3DRS_COLORWRITEENABLE, 0x0F);
    m_pDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    m_pDevice->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
    m_pDevice->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
    m_pDevice->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    m_pDevice->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, FALSE);
    m_pDevice->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    m_pDevice->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    m_pDevice->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    m_pDevice->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
    m_pDevice->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    m_pDevice->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
    m_pDevice->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    m_pDevice->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
    m_pDevice->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
    m_pDevice->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
    m_pDevice->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
    const HRESULT result = m_pDevice->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, vertices, sizeof(Vertex));
    m_cacheState->Apply();
    return SUCCEEDED(result);
}

void CChat::RenderToSurface()
{
    ZoneScoped;
    Draw();
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
    ZoneScoped;
    if (!text)
        return;

    // Convenience overload for literals. The original API takes a writable text
    // buffer and sanitizes it in place.
    char buffer[MAX_TEXT_LENGTH + 1]{};
    CopyCString(buffer, sizeof(buffer), text);
    AddChatMessage(prefix, prefixColor, buffer);
}

void CChat::ExpandControlKeyTokens(char* text, std::size_t capacity)
{
    ZoneScoped;
    if (!text || capacity == 0 || !std::strstr(text, "~k~"))
        return;

    // R5 helper 0xB6100 does this only for strings shorter than 400 bytes,
    // expands GTA control-key tokens through gta_sa.exe 0x69E160, then copies
    // the result back only when it fits the caller supplied capacity.
    if (std::strlen(text) >= 400)
        return;

    char expanded[2052]{};
    const auto encoded = text::ToServer(text);
    std::memcpy(expanded, encoded.c_str(), std::min(encoded.size(), sizeof(expanded) - 1));

    CGame::ExpandControlKeys(expanded);

    const auto decoded = text::FromServer(expanded);
    if (decoded.size() < capacity)
        std::memcpy(text, decoded.c_str(), decoded.size() + 1);
}

void CChat::AddMessage(D3DCOLOR color, char* text)
{
    ZoneScoped;
    if (!text)
        return;

    if (std::strlen(text) > MAX_TEXT_LENGTH)
        return;

    FilterOutInvalidChars(text);
    char buffer[MAX_TEXT_LENGTH + 1]{};
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

    char buffer[MAX_TEXT_LENGTH + 1]{};
    CopyCString(buffer, sizeof(buffer), text);
    AddMessage(color, buffer);
}

void CChat::PrintV(int type, D3DCOLOR color, const char* format, va_list args)
{
    ZoneScoped;
    if (!format)
        return;

    char buffer[MAX_TEXT_LENGTH + 1]{};
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
    ZoneScoped;
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
    ZoneScoped;
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
    ZoneScoped;
    buffer[0] = '\0';

    const std::time_t value = static_cast<std::time_t>(timestamp);
    std::tm local{};
    if (localtime_s(&local, &value) != 0)
        return;

    std::strftime(buffer, sizeof(buffer), "[%H:%M:%S]", &local);
}
