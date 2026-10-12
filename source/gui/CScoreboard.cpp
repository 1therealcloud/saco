#include <stdafx.h>
#include "game/Profiler.h"

#include "CScoreboard.h"

#include "CGui.h"
#include "render/CFonts.h"
#include "render/render.h"
#include "main.h"
#include "net/CNetGame.h"
#include "pools/CPlayerPool.h"
#include "players/CLocalPlayer.h"
#include "players/CRemotePlayer.h"

#include <cmath>
#include <cstdio>

namespace
{
    constexpr D3DCOLOR kHeaderTextColor = 0xFFBEBEBEu;
    constexpr D3DCOLOR kColumnTextColor = 0xFF95B0D0u;

    constexpr float kLargeWidth = 800.0f;
    constexpr float kLargeHeight = 600.0f;
    constexpr float kSmallWidth = 640.0f;
    constexpr float kSmallHeight = 480.0f;

    constexpr float kHeaderHeight = 60.0f;
    constexpr float kIdColumnWidth = 0.085f;
    constexpr float kScoreColumnWidth = 0.265625f;
    constexpr float kNameColumnWidth = 0.4375f;

    constexpr float kColumnLeft = 10.0f;
    constexpr float kColumnRight = 56.0f;

    constexpr int kHeaderFormat = DT_SINGLELINE | DT_NOCLIP;
    constexpr std::uint8_t kClickSourceScoreboard = 0;

    struct RuntimeState
    {
        std::vector<CScoreboard::PlayerInfo> players;
        std::vector<CScoreboard::PlayerInfo> snapshot;
        ULONGLONG nextRefresh = 0;
        std::vector<std::uint16_t> visibleIds;
        std::string serverName;
    };

    std::unordered_map<CScoreboard*, RuntimeState> g_runtime;
    RuntimeState& State(CScoreboard* scoreboard)
    {
        return g_runtime[scoreboard];
    }

    bool GetClientSize(int& width, int& height)
    {
        const RECT rect = CGui::GetClientRect();
        width = rect.right - rect.left;
        height = rect.bottom - rect.top;
        return width > 0 && height > 0;
    }

    int ColumnWidth(float fraction, float width)
    {
        return static_cast<int>(std::lround(fraction * width));
    }

    void SortPlayers(std::vector<CScoreboard::PlayerInfo>& players, CScoreboard::SortMode mode)
    {
        if (mode == CScoreboard::SORT_NAME)
        {
            std::stable_sort(players.begin(), players.end(), [](const CScoreboard::PlayerInfo& left, const CScoreboard::PlayerInfo& right) {
                if (left.local != right.local)
                    return left.local;
                return std::strcmp(left.name.c_str(), right.name.c_str()) < 0;
            });
        }
        else if (mode == CScoreboard::SORT_SCORE)
        {
            std::stable_sort(players.begin(), players.end(), [](const CScoreboard::PlayerInfo& left, const CScoreboard::PlayerInfo& right) {
                if (left.local != right.local)
                    return left.local;
                return left.score > right.score;
            });
        }
    }
}

CScoreboard::CScoreboard(IDirect3DDevice9* device)
    : m_bIsEnabled(FALSE), m_nPlayerCount(0), m_position{0.0f, 0.0f}, m_fScalar(1.0f), m_size{0.0f, 0.0f}, m_fHeaderHeight(kHeaderHeight), m_fUnknown(0.0f),
      m_fIdColumnWidth(kIdColumnWidth), m_fScoreColumnWidth(kScoreColumnWidth), m_fNameColumnWidth(kNameColumnWidth), m_pDevice(device), m_pDialog(nullptr), m_pListbox(nullptr),
      m_nCurrentOffset(0), m_bIsSorted(SORT_NONE)
{
    int width = 0;
    int height = 0;

    if (GetClientSize(width, height) && width > 800)
    {
        m_size[0] = kLargeWidth;
        m_size[1] = kLargeHeight;
    }
    else
    {
        m_size[0] = kSmallWidth;
        m_size[1] = kSmallHeight;
    }

    State(this);
    Recalc();

    if (CGui::IsInitialized())
        ResetDialogControls(CGui::GetDialog(CGui::DialogId::Scoreboard));
}

CScoreboard::~CScoreboard()
{
    Close(true);
    if (m_pDialog)
    {
        m_pDialog->SetCallback(nullptr, nullptr);
        m_pDialog->RemoveControl(LISTBOX_ID);
    }

    g_runtime.erase(this);

    m_pListbox = nullptr;
    m_pDialog = nullptr;
}

void CScoreboard::Recalc()
{
    int width = 0;
    int height = 0;

    if (!GetClientSize(width, height))
        return;

    // Exact R5 behavior: scalar is always reset to 1.0 and the fixed-size
    // scoreboard is centred in the current GTA client rectangle.
    m_fScalar = 1.0f;
    m_position[0] = static_cast<float>(width) * 0.5f - m_size[0] * 0.5f;
    m_position[1] = static_cast<float>(height) * 0.5f - m_size[1] * 0.5f;
    if (m_pDialog)
    {
        const RECT current = m_pDialog->GetRect();
        const RECT expected{static_cast<LONG>(m_position[0]), static_cast<LONG>(m_position[1]), static_cast<LONG>(m_position[0] + m_size[0]),
                            static_cast<LONG>(m_position[1] + m_size[1])};
        if (!EqualRect(&current, &expected))
        {
            m_pDialog->SetLocation(expected.left, expected.top);
            m_pDialog->SetSize(static_cast<int>(m_size[0]), static_cast<int>(m_size[1]));
        }
    }
}

void CScoreboard::GetRect(RECT* rect) const
{
    if (!rect)
        return;

    rect->left = static_cast<LONG>(m_position[0]);
    rect->top = static_cast<LONG>(m_position[1]);
    rect->right = static_cast<LONG>(m_position[0] + m_size[0]);
    rect->bottom = static_cast<LONG>(m_position[1] + m_size[1]);
}

void CScoreboard::Close(bool hideCursor)
{
    if (!m_bIsEnabled || !m_pDialog || !m_pListbox)
        return;

    m_pDialog->RequestFocus(nullptr);
    m_pDialog->SetVisible(false);
    m_pListbox->SetVisible(false);
    m_pListbox->SetEnabled(false);

    m_bIsEnabled = FALSE;
    if (hideCursor)
        render::SetCursorMode(0, false);
}

void CScoreboard::ResetDialogControls(cgui::Dialog* dialog)
{
    if (m_pDialog && m_pListbox)
        m_pDialog->RemoveControl(LISTBOX_ID);

    m_pDialog = dialog;
    m_pListbox = nullptr;

    if (!m_pDialog)
        return;

    Recalc();

    const int width = static_cast<int>(m_size[0]);
    const int height = static_cast<int>(m_size[1]);
    const int header = static_cast<int>(m_fHeaderHeight);

    m_pDialog->SetSize(width, height);
    m_pDialog->SetLocation(static_cast<int>(m_position[0]), static_cast<int>(m_position[1]));
    m_pDialog->EnableMouseInput(true);
    m_pDialog->EnableKeyboardInput(true);
    m_pDialog->SetCallback(&CScoreboard::GuiEvent, this);

    m_pListbox = m_pDialog->AddListBox(LISTBOX_ID, 0, header, width, std::max(0, height - header));

    if (!m_pListbox)
        return;

    m_pListbox->SetTextPadding(kColumnLeft);
    m_pListbox->SetColumnWidth(0, ColumnWidth(m_fIdColumnWidth, m_size[0]));
    m_pListbox->SetColumnWidth(1, ColumnWidth(m_fNameColumnWidth, m_size[0]));
    m_pListbox->SetColumnWidth(2, ColumnWidth(m_fScoreColumnWidth, m_size[0]));
    m_pListbox->SetVisible(false);
    m_pListbox->SetEnabled(false);
    m_pDialog->SetVisible(false);

    RuntimeState& state = State(this);
    state.nextRefresh = 0;
    state.players.clear();
    state.visibleIds.clear();

    Recalc();
}

void CScoreboard::SendNotification()
{
    if (!m_bIsEnabled || !m_pDialog || !m_pListbox)
        return;

    const int selected = m_pListbox->GetSelectedIndex();
    const RuntimeState& state = State(this);

    if (selected < 0 || selected >= static_cast<int>(state.visibleIds.size()))
        return;

    const std::uint16_t clickedPlayer = state.visibleIds[selected];

    // send click rpc
    if (g_netGame)
        g_netGame->ClickPlayer(clickedPlayer, kClickSourceScoreboard);

    // samp closes the scoreboard after the click-player notification path
    Close(true);
}

void CScoreboard::UpdateList()
{
    PROFILE_SCOPE(Scoreboard);
    if (!m_bIsEnabled || !m_pDialog || !m_pListbox)
        return;

    RuntimeState& state = State(this);

    const int selected = m_pListbox->GetSelectedIndex();
    const int selectedId = selected >= 0 && selected < static_cast<int>(state.visibleIds.size()) ? state.visibleIds[selected] : -1;
    const auto now = GetTickCount64();
    if (now < state.nextRefresh)
        return;
    state.nextRefresh = now + 100;
    auto& players = state.snapshot;
    players.clear();
    auto* pools = g_netGame ? g_netGame->m_pPools : nullptr;
    auto* playerPool = pools ? pools->m_pPlayer : nullptr;
    const bool connected = g_netGame && g_netGame->GetState() == CNetGame::GAME_MODE_CONNECTED;
    state.serverName = g_netGame ? g_netGame->m_szHostname : "SA-MP";

    if (playerPool)
    {
        PlayerInfo local{};
        local.local = true;
        local.id = connected && playerPool->m_localInfo.m_nId != CPlayerPool::INVALID_ID ? playerPool->m_localInfo.m_nId : 0;
        local.name = playerPool->GetLocalPlayerName() ? playerPool->GetLocalPlayerName() : "";
        local.score = connected ? playerPool->GetLocalPlayerScore() : 0;
        local.ping = connected ? playerPool->GetLocalPlayerPing() : 0;
        if (auto* localPlayer = playerPool->GetLocalPlayer())
            local.color = localPlayer->GetColorAsARGB();
        players.push_back(std::move(local));

        if (connected)
        {
            for (int id = 0; id <= playerPool->m_nLargestId; ++id)
            {
                const auto playerId = static_cast<CPlayerPool::ID>(id);
                if (playerId == playerPool->m_localInfo.m_nId || !playerPool->IsConnected(playerId))
                    continue;

                PlayerInfo row{};
                row.id = playerId;
                row.name = playerPool->GetName(playerId) ? playerPool->GetName(playerId) : "";
                row.score = playerPool->GetScore(playerId);
                row.ping = playerPool->GetPing(playerId);
                if (auto* player = playerPool->GetPlayer(playerId))
                    row.color = player->GetColorAsARGB();
                row.npc = playerPool->IsNPC(playerId) != FALSE;
                players.push_back(std::move(row));
            }
        }
    }

    players.erase(std::remove_if(players.begin(), players.end(),
                                 [](const PlayerInfo& player) {
                                     return !player.connected || player.npc;
                                 }),
                  players.end());
    if (players.size() > MAX_PLAYERS)
        players.resize(MAX_PLAYERS);
    SortPlayers(players, GetSortMode());
    const bool unchanged =
        players.size() == state.players.size() && std::equal(players.begin(), players.end(), state.players.begin(), [](const PlayerInfo& a, const PlayerInfo& b) {
            return a.id == b.id && a.name == b.name && a.score == b.score && a.ping == b.ping && a.color == b.color;
        });
    if (unchanged)
        return;
    const int top = m_pListbox->GetTopIndex();
    const bool sameOrder =
        players.size() == state.players.size() && std::equal(players.begin(), players.end(), state.players.begin(), [](const PlayerInfo& a, const PlayerInfo& b) {
            return a.id == b.id;
        });
    if (sameOrder)
    {
        for (std::size_t row = 0; row < players.size(); ++row)
        {
            const auto& player = players[row];
            const auto& old = state.players[row];
            char color[9]{};
            std::snprintf(color, sizeof(color), "{%06X}", player.color & 0xFFFFFFu);
            if (player.color != old.color)
                m_pListbox->SetItemText(static_cast<int>(row), 0, (color + std::to_string(player.id)).c_str());
            if (player.color != old.color || player.name != old.name)
                m_pListbox->SetItemText(static_cast<int>(row), 1, (color + player.name).c_str());
            if (player.color != old.color || player.score != old.score)
                m_pListbox->SetItemText(static_cast<int>(row), 2, (color + std::to_string(player.score)).c_str());
            if (player.color != old.color || player.ping != old.ping)
                m_pListbox->SetItemText(static_cast<int>(row), 3, (color + std::to_string(player.ping)).c_str());
        }
        state.players.swap(players);
        return;
    }
    state.players.swap(players);
    state.visibleIds.clear();
    m_pListbox->RemoveAllItems();
    for (const PlayerInfo& player : state.players)
    {
        char color[9]{};
        std::snprintf(color, sizeof(color), "{%06X}", player.color & 0xFFFFFFu);
        const int row = static_cast<int>(state.visibleIds.size());
        const std::string id = color + std::to_string(player.id);
        m_pListbox->AddItem(id.c_str(), reinterpret_cast<void*>(static_cast<std::uintptr_t>(player.id)));
        m_pListbox->SetItemText(row, 1, (color + player.name).c_str());
        m_pListbox->SetItemText(row, 2, (color + std::to_string(player.score)).c_str());
        m_pListbox->SetItemText(row, 3, (color + std::to_string(player.ping)).c_str());
        state.visibleIds.push_back(player.id);
        if (player.id == selectedId)
            m_pListbox->SetSelectedIndex(static_cast<int>(state.visibleIds.size()) - 1);
    }
    m_pListbox->SetTopIndex(top);
    m_nPlayerCount = static_cast<int>(state.players.size());
}

void CScoreboard::Draw()
{
    if (!m_bIsEnabled)
        return;

    // R5 requests a fresh score/ping update while TAB is open.
    if (g_netGame && g_netGame->GetState() == CNetGame::GAME_MODE_CONNECTED)
        g_netGame->UpdatePlayers();

    DrawHeader();
}

void CScoreboard::Enable()
{
    if (m_bIsEnabled || !m_pDialog || !m_pListbox)
        return;

    m_pDialog->SetVisible(true);
    m_pListbox->SetVisible(true);
    m_pListbox->SetEnabled(true);
    m_pDialog->RequestFocus(m_pListbox);

    m_bIsEnabled = TRUE;
    State(this).nextRefresh = 0;
    UpdateList();

    render::SetCursorMode(3, false);
}

bool CScoreboard::MsgProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (!m_bIsEnabled || !m_pDialog)
        return false;

    if (message == WM_KEYDOWN && wParam == VK_ESCAPE)
    {
        Close(true);
        return true;
    }

    return m_pDialog->MsgProc(window, message, wParam, lParam);
}

void CScoreboard::SetSortMode(SortMode mode)
{
    if (mode < SORT_NONE || mode > SORT_SCORE)
        mode = SORT_NONE;

    m_bIsSorted = static_cast<BOOL>(mode);
    State(this).nextRefresh = 0;

    if (m_bIsEnabled)
        UpdateList();
}

CScoreboard::SortMode CScoreboard::GetSortMode() const
{
    switch (m_bIsSorted)
    {
        case SORT_NAME:
            return SORT_NAME;
        case SORT_SCORE:
            return SORT_SCORE;
        default:
            return SORT_NONE;
    }
}

void CScoreboard::GuiEvent(unsigned int event, int controlId, cgui::Control*, void* context)
{
    CScoreboard* scoreboard = static_cast<CScoreboard*>(context);
    if (!scoreboard || controlId != LISTBOX_ID)
        return;

    if (event == CGui::EVENT_LISTBOX_ITEM_DBLCLK)
        scoreboard->SendNotification();
}

void CScoreboard::DrawHeader()
{
    ui::OpacityScope fade(m_pDialog ? m_pDialog->GetOpacity() : 1.0f);
    CFonts* fonts = g_fonts.get();
    if (!fonts)
        return;

    RuntimeState& state = State(this);

    char players[32]{};
    std::snprintf(players, sizeof(players), "Players: %d", m_nPlayerCount);

    const LONG left = static_cast<LONG>(m_position[0] + 0.5f);
    const LONG top = static_cast<LONG>(m_position[1] + 0.5f);
    const LONG right = static_cast<LONG>(m_position[0] + m_size[0]);
    const LONG bottom = static_cast<LONG>(m_position[1] + m_fHeaderHeight);

    RECT rect{left, top, right, bottom};

    if (!state.serverName.empty())
        fonts->DrawLittleText(state.serverName.c_str(), rect, kHeaderFormat, kHeaderTextColor, true);

    SIZE maxPlayersSize{};
    fonts->GetLittleTextScreenSize(&maxPlayersSize, "Players: 10000", DT_CALCRECT | DT_SINGLELINE | DT_LEFT);

    rect.left = right - maxPlayersSize.cx;
    fonts->DrawLittleText(players, rect, kHeaderFormat, kHeaderTextColor, true);

    const int labelTop = static_cast<int>(m_position[1] + m_fHeaderHeight - fonts->GetCharHeight() - 3.0f);
    const int labelBottom = static_cast<int>(m_position[1] + m_fHeaderHeight);

    float columnLeft = m_position[0] + m_fScalar * kColumnLeft;
    float columnRight = m_position[0] + m_fScalar * kColumnRight;

    auto drawColumn = [&](const char* text) {
        RECT column{static_cast<LONG>(columnLeft), labelTop, static_cast<LONG>(columnRight), labelBottom};

        fonts->DrawLittleText(text, column, kHeaderFormat, kColumnTextColor, true);
    };

    drawColumn("id");

    const float idWidth = ColumnWidth(m_fIdColumnWidth, m_size[0]);
    columnLeft += idWidth;
    columnRight += idWidth;
    drawColumn("name");

    const float nameWidth = ColumnWidth(m_fNameColumnWidth, m_size[0]);
    columnLeft += nameWidth;
    columnRight += nameWidth;
    drawColumn("score");

    const float scoreWidth = ColumnWidth(m_fScoreColumnWidth, m_size[0]);
    columnLeft += scoreWidth;
    columnRight += scoreWidth;
    drawColumn("ping");
}
