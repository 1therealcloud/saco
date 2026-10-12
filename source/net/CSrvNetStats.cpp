#include <stdafx.h>

#include "CSrvNetStats.h"

#include "CNetGame.h"
#include "main.h"
#include "render/CFonts.h"

#include <cstdio>

namespace
{
    constexpr D3DCOLOR kTitleColor = 0xFF8888EEu;
    constexpr D3DCOLOR kTextColor = 0xFFFFFFFFu;
    constexpr float kBytesToKilobytes = 1.0f / 1024.0f;
    constexpr std::uint32_t kInitialResponseTimeout = 1000;
    constexpr std::uint32_t kPollInterval = 1000;

    RakNetStatisticsStruct g_serverStatistics{};
    bool g_active = false;
    bool g_hasStatistics = false;
    bool g_hasProcessedStatistics = false;
    bool g_initialRequestSent = false;
    std::uint32_t g_statisticsSerial = 0;
    std::uint32_t g_lastProcessedSerial = 0;
    std::uint32_t g_initialRequestTick = 0;
    std::uint32_t g_lastResponseTick = 0;
    std::uint32_t g_lastRequestTick = 0;
}

CSrvNetStats::CSrvNetStats(IDirect3DDevice9* device)
    : m_dwLastTotalBytesSent(0), m_dwLastTotalBytesRecv(0), m_dwLastUpdateTick(0), m_dwBPSUpload(0), m_dwBPSDownload(0), m_pDevice(device)
{
    Reset();
}

void CSrvNetStats::Draw()
{
    if (!g_active)
        return;

    const std::uint32_t now = ::GetTickCount();

    if (!g_hasStatistics)
    {
        if (!g_initialRequestSent && g_netGame && g_netGame->SrvNetStats())
        {
            g_initialRequestSent = true;
            g_initialRequestTick = now;
            g_lastRequestTick = now;
        }

        if (g_initialRequestSent && now - g_initialRequestTick >= kInitialResponseTimeout)
            return;

        return;
    }

    if (g_lastRequestTick == 0 || now - g_lastRequestTick >= kPollInterval)
    {
        if (g_netGame && g_netGame->SrvNetStats())
            g_lastRequestTick = now;
    }

    if (!g_fonts || !m_pDevice)
        return;

    if (g_lastProcessedSerial != g_statisticsSerial)
    {
        const auto totalBytesSent = static_cast<std::uint32_t>(g_serverStatistics.totalBitsSent >> 3);
        const auto totalBytesRecv = static_cast<std::uint32_t>(g_serverStatistics.bitsReceived >> 3);

        if (g_hasProcessedStatistics)
        {
            const std::uint32_t elapsed = g_lastResponseTick - m_dwLastUpdateTick;
            if (elapsed != 0)
            {
                const std::uint32_t sentDelta = totalBytesSent - m_dwLastTotalBytesSent;
                const std::uint32_t recvDelta = totalBytesRecv - m_dwLastTotalBytesRecv;

                m_dwBPSDownload = static_cast<std::uint32_t>((static_cast<std::uint64_t>(sentDelta) * 1000u) / elapsed);
                m_dwBPSUpload = static_cast<std::uint32_t>((static_cast<std::uint64_t>(recvDelta) * 1000u) / elapsed);
            }
        }
        else
        {
            m_dwBPSDownload = 0;
            m_dwBPSUpload = 0;
            g_hasProcessedStatistics = true;
        }

        m_dwLastTotalBytesSent = totalBytesSent;
        m_dwLastTotalBytesRecv = totalBytesRecv;
        m_dwLastUpdateTick = g_lastResponseTick;
        g_lastProcessedSerial = g_statisticsSerial;
    }

    char text[8192]{};
    std::snprintf(text, sizeof(text),
                  "Download Rate: %.2f KB/s\n"
                  "Upload Rate: %.2f KB/s\n",
                  static_cast<float>(m_dwBPSDownload) * kBytesToKilobytes, static_cast<float>(m_dwBPSUpload) * kBytesToKilobytes);

    char rakNetText[4096]{};
    StatisticsToString(&g_serverStatistics, rakNetText, 1);
    strncat_s(text, sizeof(text), rakNetText, _TRUNCATE);

    D3DVIEWPORT9 viewport{};
    if (FAILED(m_pDevice->GetViewport(&viewport)))
        return;

    const LONG left = static_cast<LONG>(viewport.X) + 10;
    const LONG top = static_cast<LONG>(viewport.Y) + 10;
    const LONG right = static_cast<LONG>(viewport.X + viewport.Width) - 10;
    const LONG bottom = static_cast<LONG>(viewport.Y + viewport.Height) - 10;

    RECT titleRect{left, top, right, bottom};
    g_fonts->DrawText("Server Network Stats", titleRect, kTitleColor, true);

    RECT textRect{left, top + g_fonts->GetCharHeight(), right, bottom};
    g_fonts->DrawText(text, textRect, kTextColor, true);
}

void CSrvNetStats::SetActive(bool active)
{
    if (g_active == active)
        return;

    g_active = active;

    if (!active)
    {
        if (!g_hasStatistics)
        {
            g_initialRequestSent = false;
            g_initialRequestTick = 0;
            g_lastRequestTick = 0;
        }
        return;
    }

    if (g_hasStatistics)
        g_lastRequestTick = 0;
}

bool CSrvNetStats::HasStatistics() const
{
    return g_hasStatistics;
}

void CSrvNetStats::Reset()
{
    std::memset(&g_serverStatistics, 0, sizeof(g_serverStatistics));
    g_active = false;
    g_hasStatistics = false;
    g_hasProcessedStatistics = false;
    g_initialRequestSent = false;
    g_statisticsSerial = 0;
    g_lastProcessedSerial = 0;
    g_initialRequestTick = 0;
    g_lastResponseTick = 0;
    g_lastRequestTick = 0;
}

RakNetStatisticsStruct& CSrvNetStats::RefStatistics()
{
    return g_serverStatistics;
}

void CSrvNetStats::StoreStatistics(const RakNetStatisticsStruct& statistics)
{
    g_serverStatistics = statistics;
    g_lastResponseTick = ::GetTickCount();
    ++g_statisticsSerial;
    g_hasStatistics = true;
}
