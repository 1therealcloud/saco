#include <stdafx.h>
#include "Profiler.h"

#if defined(_DEBUG)
#   include "main.h"
#   include "game/CGame.h"
#   include "net/CNetGame.h"
#   include "pools/CPlayerPool.h"
#   include "pools/CVehiclePool.h"
#   include "pools/CObjectPool.h"
#   include "pools/CActorPool.h"
#   include "render/CFonts.h"
#   include <RakClientInterface.h>
#   include <RakNetStatistics.h>
#   include <Psapi.h>

namespace profiler
{
    namespace
    {
        constexpr std::size_t kHistory = 120;
        constexpr std::size_t kSections = static_cast<std::size_t>(Section::Count);
        constexpr std::size_t kCounters = static_cast<std::size_t>(Counter::Count);
        constexpr std::array<const char*, kSections> kNames{
            "Main process", "Net + pools",      "Receive / RPC", "Local player",  "Remote players", "Object pool",     "Vehicle pool", "Pickup pool",
            "Audio",        "Building removal", "UI total",      "Materials",     "Model previews", "Name tags",       "Chat bubbles", "3D labels",
            "Chat",         "TAB update",       "GUI controls",  "Dialog text",   "Death window",   "Object editor",   "Menus",        "Native textdraws",
            "IME",          "GTA ped controls", "World render",  "Fading render", "Present / wait", "Profiler overlay"};
        struct Sample
        {
            std::array<double, kSections> ms{};
            std::array<unsigned, kSections> calls{};
            std::array<unsigned long long, kCounters> counters{};
            double period = 0;
        };
        Sample g_pending;
        std::array<Sample, kHistory> g_history{};
        std::size_t g_count = 0, g_next = 0;
        bool g_enabled = false, g_suspended = false;
        DWORD g_thread = 0;
        long long g_previousFrame = 0;
        double g_tickMs = 0;
        ULONGLONG g_lastUpdate = 0;
        std::string g_text;
        SIZE g_panelSize{};
        unsigned long long g_lastCpu = 0, g_lastRx = 0, g_lastTx = 0;
        long long g_lastStats = 0;
        const CNetGame* g_lastNetGame = nullptr;

        long long Now()
        {
            LARGE_INTEGER value{};
            QueryPerformanceCounter(&value);
            return value.QuadPart;
        }
        bool Recording()
        {
            return g_enabled && !g_suspended && GetCurrentThreadId() == g_thread;
        }
        void UpdateText()
        {
            std::array<double, kHistory> values{};
            char line[256]{};
            g_text.clear();
            double period = 0;
            for (std::size_t i = 0; i < g_count; ++i)
                period += g_history[i].period;
            period = g_count ? period / g_count : 0;
            std::snprintf(line, sizeof(line), "%.1f FPS | %.3f ms/frame | %zu/120 frames\nCPU timings: inclusive, ms/frame; GPU time is not measured\n",
                          period > 0 ? 1000 / period : 0, period, g_count);
            g_text += line;
            g_text += "Section                 avg       p95       max     calls/f\n";
            for (std::size_t section = 0; section < kSections && g_count; ++section)
            {
                double sum = 0, calls = 0;
                for (std::size_t i = 0; i < g_count; ++i)
                {
                    values[i] = g_history[i].ms[section];
                    sum += values[i];
                    calls += g_history[i].calls[section];
                }
                if (!calls)
                    continue;
                std::sort(values.begin(), values.begin() + g_count);
                const auto p95 = static_cast<std::size_t>(std::ceil(g_count * .95)) - 1;
                std::snprintf(line, sizeof(line), "%-22s %8.3f  %8.3f  %8.3f  %7.1f\n", kNames[section], sum / g_count, values[p95], values[g_count - 1], calls / g_count);
                g_text += line;
            }
            std::array<double, kCounters> counters{};
            for (std::size_t i = 0; i < g_count; ++i)
                for (std::size_t c = 0; c < kCounters; ++c)
                    counters[c] += static_cast<double>(g_history[i].counters[c]);
            for (double& value : counters)
                value /= std::max<std::size_t>(g_count, 1);
            auto count = [&](Counter c) {
                return counters[static_cast<std::size_t>(c)];
            };
            std::snprintf(line, sizeof(line),
                          "\nDraws/frame: fonts %.1f / CGui %.1f | vertices %.0f / %.0f\nText cache/frame: hits %.1f / misses %.1f\nReceived/frame: %.1f packets / %.0f bytes\n",
                          count(Counter::FontDraws), count(Counter::GuiDraws), count(Counter::FontVertices), count(Counter::GuiVertices), count(Counter::CacheHits),
                          count(Counter::CacheMisses), count(Counter::Packets), count(Counter::PacketBytes));
            g_text += line;
            std::snprintf(line, sizeof(line), "CRender: %.1f draws / %.0f vertices | IME: %.1f draws/frame\n", count(Counter::SolidDraws), count(Counter::SolidVertices),
                          count(Counter::ImeDraws));
            g_text += line;
            const auto statsTime = Now();
            const double elapsedMs = g_lastStats ? (statsTime - g_lastStats) * g_tickMs : 0;
            FILETIME created{}, exited{}, kernel{}, user{};
            if (GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user))
            {
                const unsigned long long cpu = ((static_cast<unsigned long long>(kernel.dwHighDateTime) << 32) | kernel.dwLowDateTime) +
                                               ((static_cast<unsigned long long>(user.dwHighDateTime) << 32) | user.dwLowDateTime);
                SYSTEM_INFO system{};
                GetSystemInfo(&system);
                const double load = elapsedMs > 0 && g_lastCpu ? (cpu - g_lastCpu) / (elapsedMs * 100.0 * std::max<DWORD>(system.dwNumberOfProcessors, 1)) : 0;
                std::snprintf(line, sizeof(line), "Process CPU: %.1f%% of %lu logical CPUs\n", load, system.dwNumberOfProcessors);
                g_text += line;
                g_lastCpu = cpu;
            }
            PROCESS_MEMORY_COUNTERS_EX memory{};
            memory.cb = sizeof(memory);
            if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory)))
            {
                std::snprintf(line, sizeof(line), "Memory: working %.1f MB / private %.1f MB\n", memory.WorkingSetSize / 1048576.0, memory.PrivateUsage / 1048576.0);
                g_text += line;
            }
            if (g_game)
            {
                std::snprintf(line, sizeof(line), "Streaming: used %.1f MB / budget %.1f MB\n", g_game->GetUsedMemory() / 1048576.0, g_game->GetStreamingMemory() / 1048576.0);
                g_text += line;
            }
            auto* pools = g_netGame ? g_netGame->m_pPools : nullptr;
            int vehicles = 0, actors = 0;
            if (pools && pools->m_pVehicle)
                for (int i = 0; i < CVehiclePool::MAX_VEHICLES; ++i)
                    vehicles += pools->m_pVehicle->m_bNotEmpty[i] != FALSE;
            if (pools && pools->m_pActor)
                for (int i = 0; i < CActorPool::MAX_ACTORS; ++i)
                    actors += pools->m_pActor->m_bNotEmpty[i] != FALSE;
            std::snprintf(line, sizeof(line), "Net state %d | remote players %d | vehicles %d | objects %d | actors %d\n", g_netGame ? g_netGame->GetState() : -1,
                          pools && pools->m_pPlayer ? pools->m_pPlayer->GetCount(TRUE) : 0, vehicles, pools && pools->m_pObject ? pools->m_pObject->GetCount() : 0, actors);
            g_text += line;
            // RakClient::GetStatistics requires an established remote system.
            if (g_netGame && g_netGame->m_pRakClient && g_netGame->m_pRakClient->IsConnected())
            {
                auto* stats = g_netGame->m_pRakClient->GetStatistics();
                if (stats)
                {
                    const bool sameConnection = g_lastNetGame == g_netGame.get();
                    const double rx = elapsedMs > 0 && sameConnection && stats->bitsReceived >= g_lastRx ? (stats->bitsReceived - g_lastRx) / (8.192 * elapsedMs) : 0;
                    const double tx = elapsedMs > 0 && sameConnection && stats->totalBitsSent >= g_lastTx ? (stats->totalBitsSent - g_lastTx) / (8.192 * elapsedMs) : 0;
                    std::snprintf(line, sizeof(line), "RakNet: RX %.1f / TX %.1f KB/s | ping %d ms\n", rx, tx, g_netGame->m_pRakClient->GetLastPing());
                    g_text += line;
                    g_lastNetGame = g_netGame.get();
                    g_lastRx = stats->bitsReceived;
                    g_lastTx = stats->totalBitsSent;
                }
            }
            else
                g_lastNetGame = nullptr;
            g_lastStats = statsTime;
            g_text += "Overlapping sections are not additive. Profiler draws excluded from counters.";
        }
    }

    Scope::Scope(Section section) : m_section(section), m_start(Recording() ? Now() : 0)
    {}
    Scope::~Scope()
    {
        Stop();
    }
    void Scope::Stop()
    {
        if (!m_start)
            return;
        if (Recording())
        {
            const auto index = static_cast<std::size_t>(m_section);
            g_pending.ms[index] += (Now() - m_start) * g_tickMs;
            ++g_pending.calls[index];
        }
        m_start = 0;
    }
    void BeginFrame()
    {
        const bool enabled = (GetAsyncKeyState(VK_F2) & 0x8000) != 0;
        if (enabled && !g_enabled)
        {
            LARGE_INTEGER frequency{};
            QueryPerformanceFrequency(&frequency);
            g_tickMs = 1000.0 / frequency.QuadPart;
            g_thread = GetCurrentThreadId();
            g_pending = {};
            g_count = g_next = 0;
            g_previousFrame = 0;
            g_lastUpdate = 0;
            g_lastCpu = g_lastRx = g_lastTx = 0;
            g_lastStats = 0;
            g_lastNetGame = nullptr;
            g_text.clear();
        }
        g_enabled = enabled;
        if (!enabled)
            return;
        const auto now = Now();
        g_pending.period = g_previousFrame ? (now - g_previousFrame) * g_tickMs : 0;
        g_previousFrame = now;
    }
    void EndFrame()
    {
        if (!g_enabled)
            return;
        // The first activation interval contains only a partial frame.
        if (g_pending.period > 0)
        {
            g_history[g_next] = g_pending;
            g_next = (g_next + 1) % kHistory;
            g_count = std::min(g_count + 1, kHistory);
        }
        g_pending = {};
    }
    bool Visible()
    {
        return g_enabled;
    }
    void Add(Counter counter, unsigned long long amount)
    {
        if (Recording())
            g_pending.counters[static_cast<std::size_t>(counter)] += amount;
    }
    void Draw(IDirect3DDevice9* device)
    {
        if (!g_enabled || !device || !g_fonts)
            return;
        const auto start = Now();
        g_suspended = true;
        const auto tick = GetTickCount64();
        if (g_text.empty() || tick - g_lastUpdate >= 500)
        {
            UpdateText();
            // Reuse the font geometry cache; layout changes only with the half-second snapshot.
            SIZE titleSize{};
            g_fonts->GetTextScreenSize(&g_panelSize, g_text.c_str(), DT_NOCLIP);
            g_fonts->GetTextScreenSize(&titleSize, "Client Profiler (F2)", DT_NOCLIP);
            g_panelSize.cx = std::max(g_panelSize.cx, titleSize.cx);
            g_panelSize.cy += g_fonts->GetCharHeight();
            g_lastUpdate = tick;
        }
        D3DVIEWPORT9 viewport{};
        if (SUCCEEDED(device->GetViewport(&viewport)))
        {
            RECT title{static_cast<LONG>(viewport.X) + 10, static_cast<LONG>(viewport.Y) + 10, static_cast<LONG>(viewport.X + viewport.Width) - 10,
                       static_cast<LONG>(viewport.Y + viewport.Height) - 10};
            CFont::Batch batch(g_fonts->GetFont());
            // One background quad replaces four outline copies for every glyph.
            g_fonts->GetFont()->DrawBox(static_cast<float>(title.left - 4), static_cast<float>(title.top - 4), static_cast<float>(g_panelSize.cx + 8),
                                        static_cast<float>(g_panelSize.cy + 8), 0xC0000000u);
            g_fonts->DrawText("Client Profiler (F2)", title, 0xFF8888EEu, false);
            title.top += g_fonts->GetCharHeight();
            g_fonts->DrawText(g_text.c_str(), title, 0xFFFFFFFFu, false);
        }
        g_suspended = false;
        const auto index = static_cast<std::size_t>(Section::Overlay);
        g_pending.ms[index] += (Now() - start) * g_tickMs;
        ++g_pending.calls[index];
    }
}
#endif
