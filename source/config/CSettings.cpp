#include <stdafx.h>

#include <filesystem>

#include "CSettings.h"
#include "main.h"

// read registry string
static bool ReadRegistryString(HKEY key, const char* name, char* destination, DWORD capacity)
{
    char value[MAX_PATH + 1]{};
    DWORD size = sizeof(value);
    if (RegGetValueA(key, nullptr, name, RRF_RT_REG_SZ, nullptr, value, &size) != ERROR_SUCCESS || size > capacity)
        return false;
    std::memcpy(destination, value, size);
    destination[capacity - 1] = '\0';
    return true;
}

// returns true if the space exists
static bool IsSpace(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

// read argument
static const char* ReadArgument(const char* line, char* string)
{
    while (IsSpace(*line))
        ++line;

    const bool quoted = *line == '"';

    if (quoted)
        ++line;

    std::size_t length = 0;

    while (*line && (quoted ? *line != '"' : !IsSpace(*line)))
    {
        if (string && length < CSettings::SETTINGS_STRING_LEN)
            string[length++] = *line;
        ++line;
    }

    if (string)
        string[length] = '\0';

    if (quoted && *line == '"')
        ++line;

    return line;
}

// get argument from command line
void CSettings::GetFromCommandLine(const char* line, char* string)
{
    if (!string)
        return;

    string[0] = '\0';

    if (line)
        ReadArgument(line, string);
}

// get argument from quotes
void CSettings::GetFromQuotes(const char* line, char* string)
{
    if (!string)
        return;

    string[0] = '\0';

    if (line)
    {
        const char* quote = std::strchr(line, '"');
        if (quote)
            ReadArgument(quote, string);
    }
}

// wrapper
void CSettings::Initialize()
{
    const char* line = GetCommandLineA();

    while (IsSpace(*line))
        ++line;

    if (*line != '-' && *line != '/')
        line = ReadArgument(line, nullptr);

    Initialize(line);

    if (!*m_szModelCache)
        return;

    std::error_code error;
    std::filesystem::create_directories(std::filesystem::path(m_szModelCache) / "local", error);

    if (error)
        Log("[settings] failed to create model cache: %s", error.message().c_str());
}

// init
void CSettings::Initialize(const char* arguments)
{
    *this = CSettings{};
    const char* line = arguments ? arguments : "";
    while (*line)
    {
        while (IsSpace(*line))
            ++line;

        if (!*line)
            break;

        if (*line != '-' && *line != '/')
        {
            line = ReadArgument(line, nullptr);
            continue;
        }

        ++line;
        const char option = *line;

        if (!option)
            break;

        ++line;
        char* destination = nullptr;

        switch (option)
        {
            case 'd': // offline debug mode
                m_bDebugMode = TRUE;
                m_bOnlineGame = FALSE;
                break;
            case 'c': // play online
                m_bOnlineGame = TRUE;
                m_bDebugMode = FALSE;
                break;
            case 'z': // server password
                destination = m_szPass;
                break;
            case 'h': // server host or ip
                destination = m_szHost;
                break;
            case 'p': // server port
                destination = m_szPort;
                break;
            case 'n': // player nick
                destination = m_szNick;
                break;
            case 'l': // debug script path
                destination = m_szDebugScript;
                break;
            default: // skip unknown options
                line = ReadArgument(line, nullptr);
                break;
        }

        if (destination)
        {
            while (IsSpace(*line))
                ++line;
            // a following option is not a value for an empty parameter
            if (*line != '-' && *line != '/')
                line = ReadArgument(line, destination);
            else
                destination[0] = '\0';
        }
    }

    if (m_bDebugMode)
        return;

    const auto userDirectory = GetSampUserDirectory();
    if (!userDirectory.empty())
    {
        const auto cache = (std::filesystem::path(userDirectory) / "cache").string();
        if (cache.size() < sizeof(m_szModelCache))
            std::memcpy(m_szModelCache, cache.c_str(), cache.size() + 1);
    }

    HKEY key = nullptr;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\SAMP", 0, KEY_QUERY_VALUE, &key) == ERROR_SUCCESS)
    {
        ReadRegistryString(key, "artwork_proxy", m_szArtworkProxy, sizeof(m_szArtworkProxy));
        ReadRegistryString(key, "model_cache", m_szModelCache, sizeof(m_szModelCache));
        RegCloseKey(key);
    }
}
