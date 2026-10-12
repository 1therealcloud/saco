#include <stdafx.h>

#include "CMenu.h"
#include "game/Scripting.h"

namespace
{
    constexpr const char* kDummy = "DUMMY";
    constexpr const char* kTitleKey = "SAMPHED";
    constexpr const char* kHeaderKeys[CMenu::MAX_COLUMNS] = {"SAMPRW1", "SAMPRW2"};

    constexpr char kItemKeys[CMenu::MAX_MENU_ITEMS][CMenu::MAX_COLUMNS][CMenu::MAX_MENU_LINE] = {
        {"SAMP000", "SAMP100"}, {"SAMP001", "SAMP101"}, {"SAMP002", "SAMP102"}, {"SAMP003", "SAMP103"}, {"SAMP004", "SAMP104"}, {"SAMP005", "SAMP105"},
        {"SAMP006", "SAMP106"}, {"SAMP007", "SAMP107"}, {"SAMP008", "SAMP108"}, {"SAMP009", "SAMP109"}, {"SAMP010", "SAMP110"}, {"SAMP011", "SAMP111"}};

    CMenu* g_activeMenu = nullptr;

    constexpr scm::OpcodeInfo kCreatePanel{0x08D4, "sfffiiiiv"};
    constexpr scm::OpcodeInfo kRemovePanel{0x08DA, "i"};
    constexpr scm::OpcodeInfo kSetPanelColumnData{0x08DB, "iisssssssssssss"};
    constexpr scm::OpcodeInfo kSetPanelColumnWidth{0x09DB, "iii"};
    constexpr scm::OpcodeInfo kSetPanelRowEnabled{0x08D9, "iii"};
    constexpr scm::OpcodeInfo kGetPanelActiveRow{0x08D7, "iv"};

    constexpr std::uint32_t kInvalidPanel = 0xFFFFFFFFu;
    void ClearLine(char (&line)[CMenu::MAX_MENU_LINE])
    {
        std::memset(line, 0, sizeof(line));
    }

    void CopyLine(char (&destination)[CMenu::MAX_MENU_LINE], const char* source)
    {
        ClearLine(destination);

        if (!source)
            return;

        std::memcpy(destination, source, strnlen_s(source, sizeof(destination) - 1));
    }
}

CMenu::CMenu(float x, float y, char columns, float firstColumnWidth, float secondColumnWidth, const Interaction* interaction)
    : m_nId(0), m_szTitle{}, m_szItems{}, m_szHeader{}, m_fPosX(x), m_fPosY(y), m_fFirstColumnWidth(firstColumnWidth), m_fSecondColumnWidth(secondColumnWidth),
      m_nColumns(columns == 2 ? 2 : 1), m_interaction{}, m_nColumnCount{}, m_panel(kInvalidPanel)
{
    if (interaction)
        std::memcpy(&m_interaction, interaction, sizeof(m_interaction));
}

CMenu::CMenu() : CMenu(0.0f, 0.0f, 1, 0.0f, 0.0f, nullptr)
{}

CMenu::~CMenu()
{
    Hide();
}

bool CMenu::IsVisible() const
{
    return g_activeMenu == this && m_panel != kInvalidPanel;
}

void CMenu::SetTitle(const char* text)
{
    if (!IsValidText(text))
        return;

    CopyLine(m_szTitle, text);
}

void CMenu::AddItem(unsigned char column, unsigned char row, const char* text)
{
    if (!IsValidText(text) || column >= MAX_COLUMNS || row >= MAX_MENU_ITEMS)
        return;

    CopyLine(m_szItems[row][column], text);
    m_nColumnCount[column] = std::max(m_nColumnCount[column], static_cast<unsigned char>(row + 1));
}

void CMenu::SetColumnTitle(unsigned char column, const char* text)
{
    if (!IsValidText(text) || column >= MAX_COLUMNS)
        return;

    CopyLine(m_szHeader[column], text);
}

void CMenu::Hide()
{
    ZoneScoped;
    if (m_panel != kInvalidPanel)
    {
        scm::ProcessCommand(&kRemovePanel, m_panel);
        m_panel = kInvalidPanel;
    }

    if (g_activeMenu == this)
        g_activeMenu = nullptr;
}

char* CMenu::GetItem(unsigned char column, unsigned char row)
{
    return column < MAX_COLUMNS && row < MAX_MENU_ITEMS ? m_szItems[row][column] : nullptr;
}

char* CMenu::GetTitle()
{
    return m_szTitle;
}

char* CMenu::GetColumnTitle(unsigned char column)
{
    return column < MAX_COLUMNS ? m_szHeader[column] : nullptr;
}

char* CMenu::MS(unsigned char row, unsigned char column)
{
    ZoneScoped;
    if (row >= MAX_MENU_ITEMS || column >= MAX_COLUMNS)
        return const_cast<char*>(kDummy);

    if (!m_szItems[row][column][0])
        return const_cast<char*>(kDummy);

    return const_cast<char*>(GetItemKey(row, column));
}

char CMenu::GetActiveRow()
{
    ZoneScoped;
    if (!IsVisible() || !m_interaction.m_bMenu)
        return static_cast<char>(0xFF);

    std::uint32_t row = 0xFFFFFFFFu;
    scm::ProcessCommand(&kGetPanelActiveRow, m_panel, &row);

    return row < MAX_MENU_ITEMS ? static_cast<char>(row) : static_cast<char>(0xFF);
}

void CMenu::Show()
{
    ZoneScoped;
    if (g_activeMenu)
        g_activeMenu->Hide();
    g_activeMenu = this;

    const char* title = m_szTitle[0] ? kTitleKey : kDummy;

    scm::ProcessCommand(&kCreatePanel, title, static_cast<double>(m_fPosX), static_cast<double>(m_fPosY),
                        static_cast<double>((m_fFirstColumnWidth + m_fSecondColumnWidth) / m_nColumns), static_cast<int>(m_nColumns), static_cast<int>(m_interaction.m_bMenu), 1, 1,
                        &m_panel);
    if (m_panel == kInvalidPanel)
    {
        g_activeMenu = nullptr;
        return;
    }

    const char* header0 = m_szHeader[0][0] ? kHeaderKeys[0] : kDummy;

    scm::ProcessCommand(&kSetPanelColumnData, m_panel, 0, header0, MS(0, 0), MS(1, 0), MS(2, 0), MS(3, 0), MS(4, 0), MS(5, 0), MS(6, 0), MS(7, 0), MS(8, 0), MS(9, 0), MS(10, 0),
                        MS(11, 0));

    scm::ProcessCommand(&kSetPanelColumnWidth, m_panel, 0, static_cast<int>(m_fFirstColumnWidth));

    if (m_nColumns == 2)
    {
        const char* header1 = m_szHeader[1][0] ? kHeaderKeys[1] : kDummy;

        scm::ProcessCommand(&kSetPanelColumnData, m_panel, 1, header1, MS(0, 1), MS(1, 1), MS(2, 1), MS(3, 1), MS(4, 1), MS(5, 1), MS(6, 1), MS(7, 1), MS(8, 1), MS(9, 1),
                            MS(10, 1), MS(11, 1));

        scm::ProcessCommand(&kSetPanelColumnWidth, m_panel, 1, static_cast<int>(m_fSecondColumnWidth));
    }

    for (unsigned char row = 0; row < MAX_MENU_ITEMS; ++row)
    {
        scm::ProcessCommand(&kSetPanelRowEnabled, m_panel, static_cast<int>(row), static_cast<int>(m_interaction.m_bRow[row]));
    }
}

const char* CMenu::ResolveTextKey(const char* key)
{
    ZoneScoped;
    if (!g_activeMenu || !key)
        return nullptr;

    if (std::strcmp(key, kTitleKey) == 0)
        return g_activeMenu->m_szTitle;

    if (std::strcmp(key, kHeaderKeys[0]) == 0)
        return g_activeMenu->m_szHeader[0];

    if (std::strcmp(key, kHeaderKeys[1]) == 0)
        return g_activeMenu->m_szHeader[1];

    if (std::strncmp(key, "SAMP", 4) != 0 || std::strlen(key) != 7)
        return nullptr;

    const unsigned char column = static_cast<unsigned char>(key[4] - '0');

    if (column >= MAX_COLUMNS || key[5] < '0' || key[5] > '9' || key[6] < '0' || key[6] > '9')
    {
        return nullptr;
    }

    const unsigned char row = static_cast<unsigned char>((key[5] - '0') * 10 + (key[6] - '0'));

    if (row >= MAX_MENU_ITEMS)
        return nullptr;

    return g_activeMenu->m_szItems[row][column];
}

bool CMenu::IsValidText(const char* text)
{
    return text && strnlen_s(text, MAX_MENU_LINE) < MAX_MENU_LINE;
}

const char* CMenu::GetItemKey(unsigned char row, unsigned char column)
{
    return kItemKeys[row][column];
}
