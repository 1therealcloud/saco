/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * The original code is copyright (c) 2026, 1therealcloud.
 */

#include <stdafx.h>

#include "CConfig.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <fstream>

namespace
{
    enum ParamType
    {
        PARAM_TYPE_INT,
        PARAM_TYPE_BOOL,
        PARAM_TYPE_STRING
    };

    struct ParamInfo
    {
        const char* name;
        ParamType type;
        int integerDefault;
        bool boolDefault;
        const char* stringDefault;
    };

    constexpr ParamInfo kParamInfo[CConfig::PARAM_COUNT] = {
        {"pagesize", PARAM_TYPE_INT, 10, false, nullptr},        {"fontsize", PARAM_TYPE_INT, 0, false, nullptr},
        {"timestamp", PARAM_TYPE_BOOL, 0, false, nullptr},       {"directmode", PARAM_TYPE_BOOL, 0, false, nullptr},
        {"fpslimit", PARAM_TYPE_INT, 48, false, nullptr},        {"disableheadmove", PARAM_TYPE_BOOL, 0, false, nullptr},
        {"multicore", PARAM_TYPE_BOOL, 0, true, nullptr},        {"ime", PARAM_TYPE_BOOL, 0, false, nullptr},
        {"audiomsgoff", PARAM_TYPE_BOOL, 0, false, nullptr},     {"audioproxyoff", PARAM_TYPE_BOOL, 0, false, nullptr},
        {"nonametagstatus", PARAM_TYPE_BOOL, 0, false, nullptr}, {"logurls", PARAM_TYPE_BOOL, 0, false, nullptr},
        {"nohudscalefix", PARAM_TYPE_BOOL, 0, false, nullptr},   {"fontweight", PARAM_TYPE_INT, 0, false, nullptr},
        {"fontface", PARAM_TYPE_STRING, 0, false, "Arial"},
    };

    std::string Trim(const std::string& value)
    {
        const std::size_t first = value.find_first_not_of(" \t\r\n");
        if (first == std::string::npos)
            return {};
        const std::size_t last = value.find_last_not_of(" \t\r\n");
        return value.substr(first, last - first + 1);
    }

    std::string StripComment(const std::string& value)
    {
        ZoneScoped;
        bool quoted = false;
        for (std::size_t i = 0; i < value.size(); ++i)
        {
            if (value[i] == '"')
                quoted = !quoted;
            else if (value[i] == ';' && !quoted)
                return value.substr(0, i);
        }
        return value;
    }

    std::string ParseString(const std::string& value)
    {
        const std::string trimmed = Trim(value);
        if (trimmed.size() >= 2 && trimmed.front() == '"' && trimmed.back() == '"')
            return trimmed.substr(1, trimmed.size() - 2);
        return trimmed;
    }

    bool ParseInt(const std::string& text, int& value)
    {
        ZoneScoped;
        if (text.empty())
            return false;
        char* end = nullptr;
        errno = 0;
        const long parsed = std::strtol(text.c_str(), &end, 10);
        if (errno == ERANGE || end == text.c_str() || *end != '\0' || parsed < std::numeric_limits<int>::min() || parsed > std::numeric_limits<int>::max())
            return false;
        value = static_cast<int>(parsed);
        return true;
    }

    bool ParseFloat(const std::string& text, float& value)
    {
        ZoneScoped;
        if (text.empty())
            return false;
        char* end = nullptr;
        errno = 0;
        const float parsed = std::strtof(text.c_str(), &end);
        if (errno == ERANGE || end == text.c_str() || *end != '\0')
            return false;
        value = parsed;
        return true;
    }

    bool IsValidParam(CConfig::eParam param)
    {
        return param >= CConfig::PARAM_PAGE_SIZE && param < CConfig::PARAM_COUNT;
    }
}

CConfig::CConfig()
{
    std::memset(this, 0, sizeof(*this));
}

CConfig::CConfig(const char* file)
{
    std::memset(this, 0, sizeof(*this));
    if (file && *file)
    {
        strncpy_s(m_szFilename, file, _TRUNCATE);
        Load();
    }
}

CConfig::~CConfig()
{
    for (int i = 0; i < MAX_ENTRIES; ++i)
        ClearEntry(i);
}

void CConfig::ClearEntry(int index)
{
    if (index < 0 || index >= MAX_ENTRIES)
        return;
    delete[] m_entry[index].m_szValue;
    std::memset(&m_entry[index], 0, sizeof(m_entry[index]));
    m_bNotEmpty[index] = FALSE;
}

void CConfig::Reset()
{
    for (int i = 0; i < MAX_ENTRIES; ++i)
        ClearEntry(i);
    std::memset(m_szFilename, 0, sizeof(m_szFilename));
    m_nFirstFree = 0;
}

void CConfig::FindFirstFree()
{
    ZoneScoped;
    m_nFirstFree = MAX_ENTRIES;
    for (int i = 0; i < MAX_ENTRIES; ++i)
    {
        if (!m_bNotEmpty[i])
        {
            m_nFirstFree = i;
            break;
        }
    }
}

int CConfig::GetIndex(const char* entry) const
{
    ZoneScoped;
    if (!entry || !*entry)
        return -1;
    for (int i = 0; i < MAX_ENTRIES; ++i)
    {
        if (m_bNotEmpty[i] && _stricmp(m_entry[i].m_szName, entry) == 0)
            return i;
    }
    return -1;
}

bool CConfig::DoesExist(const char* entry)
{
    return GetIndex(entry) >= 0;
}

int CConfig::CreateEntry(const char* name)
{
    ZoneScoped;
    if (!name || !*name)
        return -1;
    const int existing = GetIndex(name);
    if (existing >= 0)
        return existing;
    FindFirstFree();
    if (m_nFirstFree < 0 || m_nFirstFree >= MAX_ENTRIES)
        return -1;
    const int index = m_nFirstFree;
    std::memset(&m_entry[index], 0, sizeof(m_entry[index]));
    strncpy_s(m_entry[index].m_szName, name, _TRUNCATE);
    m_entry[index].m_nType = VALUE_TYPE_NONE;
    m_bNotEmpty[index] = TRUE;
    FindFirstFree();
    return index;
}

int CConfig::GetIntValue(const char* entry)
{
    const int index = GetIndex(entry);
    if (index < 0)
        return 0;
    if (m_entry[index].m_nType == VALUE_TYPE_FLOAT)
        return static_cast<int>(m_entry[index].m_fValue);
    return m_entry[index].m_nValue;
}

const char* CConfig::GetStringValue(const char* entry)
{
    const int index = GetIndex(entry);
    return index >= 0 && m_entry[index].m_nType == VALUE_TYPE_STRING && m_entry[index].m_szValue ? m_entry[index].m_szValue : "";
}

float CConfig::GetFloatValue(const char* entry)
{
    const int index = GetIndex(entry);
    if (index < 0)
        return 0.0f;
    if (m_entry[index].m_nType == VALUE_TYPE_INT)
        return static_cast<float>(m_entry[index].m_nValue);
    return m_entry[index].m_fValue;
}

BOOL CConfig::Free(const char* entry)
{
    ZoneScoped;
    const int index = GetIndex(entry);
    if (index < 0 || m_entry[index].m_bReadOnly)
        return FALSE;
    ClearEntry(index);
    if (index < m_nFirstFree)
        m_nFirstFree = index;
    return TRUE;
}

int CConfig::GetValueType(const char* entry)
{
    const int index = GetIndex(entry);
    return index >= 0 ? m_entry[index].m_nType : VALUE_TYPE_NONE;
}

CConfig::ConfigEntry* CConfig::GetEntry(int index)
{
    if (index < 0 || index >= MAX_ENTRIES || !m_bNotEmpty[index])
        return nullptr;
    return &m_entry[index];
}

int CConfig::GetType(const char* string)
{
    ZoneScoped;
    if (!string)
        return VALUE_TYPE_NONE;
    const std::string value = Trim(string);
    if (value.empty())
        return VALUE_TYPE_STRING;
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
        return VALUE_TYPE_STRING;
    int integer = 0;
    if (ParseInt(value, integer))
        return VALUE_TYPE_INT;
    float floating = 0.0f;
    if (ParseFloat(value, floating))
        return VALUE_TYPE_FLOAT;
    return VALUE_TYPE_STRING;
}

BOOL CConfig::WriteIntValue(const char* entry, int value, BOOL readOnly)
{
    ZoneScoped;
    int index = GetIndex(entry);
    if (index >= 0 && m_entry[index].m_bReadOnly)
        return FALSE;
    if (index < 0)
        index = CreateEntry(entry);
    if (index < 0)
        return FALSE;
    delete[] m_entry[index].m_szValue;
    m_entry[index].m_szValue = nullptr;
    m_entry[index].m_nType = VALUE_TYPE_INT;
    m_entry[index].m_nValue = value;
    m_entry[index].m_fValue = static_cast<float>(value);
    m_entry[index].m_bReadOnly = readOnly;
    return TRUE;
}

BOOL CConfig::WriteStringValue(const char* entry, const char* value, BOOL readOnly)
{
    ZoneScoped;
    if (!value)
        value = "";
    int index = GetIndex(entry);
    if (index >= 0 && m_entry[index].m_bReadOnly)
        return FALSE;
    if (index < 0)
        index = CreateEntry(entry);
    if (index < 0)
        return FALSE;
    const std::size_t length = std::strlen(value);
    char* copy = new (std::nothrow) char[length + 1];
    if (!copy)
        return FALSE;
    std::memcpy(copy, value, length + 1);
    delete[] m_entry[index].m_szValue;
    m_entry[index].m_szValue = copy;
    m_entry[index].m_nType = VALUE_TYPE_STRING;
    m_entry[index].m_nValue = 0;
    m_entry[index].m_fValue = 0.0f;
    m_entry[index].m_bReadOnly = readOnly;
    return TRUE;
}

BOOL CConfig::WriteFloatValue(const char* entry, float value, BOOL readOnly)
{
    ZoneScoped;
    int index = GetIndex(entry);
    if (index >= 0 && m_entry[index].m_bReadOnly)
        return FALSE;
    if (index < 0)
        index = CreateEntry(entry);
    if (index < 0)
        return FALSE;
    delete[] m_entry[index].m_szValue;
    m_entry[index].m_szValue = nullptr;
    m_entry[index].m_nType = VALUE_TYPE_FLOAT;
    m_entry[index].m_nValue = static_cast<int>(value);
    m_entry[index].m_fValue = value;
    m_entry[index].m_bReadOnly = readOnly;
    return TRUE;
}

std::string CConfig::Write(const char* entry) const
{
    ZoneScoped;
    const int index = GetIndex(entry);
    if (index < 0)
        return {};
    const ConfigEntry& configEntry = m_entry[index];
    char buffer[64]{};
    switch (configEntry.m_nType)
    {
        case VALUE_TYPE_INT:
            return std::to_string(configEntry.m_nValue);
        case VALUE_TYPE_FLOAT:
            std::snprintf(buffer, sizeof(buffer), "%g", configEntry.m_fValue);
            return buffer;
        case VALUE_TYPE_STRING:
            return std::string("\"") + (configEntry.m_szValue ? configEntry.m_szValue : "") + '"';
        default:
            return {};
    }
}

BOOL CConfig::Load()
{
    ZoneScoped;
    if (!m_szFilename[0])
        return FALSE;

    // Clear live entries without disturbing the selected filename.
    for (int i = 0; i < MAX_ENTRIES; ++i)
        ClearEntry(i);
    m_nFirstFree = 0;

    std::ifstream file(m_szFilename);
    if (!file.is_open())
    {
        const DWORD attributes = GetFileAttributesA(m_szFilename);
        const DWORD error = GetLastError();
        if (attributes != INVALID_FILE_ATTRIBUTES || (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND))
            return FALSE;
        ApplyPersistedDefaults();
        return Save();
    }

    std::string line;
    bool firstLine = true;
    while (std::getline(file, line))
    {
        if (firstLine && line.starts_with("\xEF\xBB\xBF"))
            line.erase(0, 3);
        firstLine = false;
        line = Trim(StripComment(line));
        if (line.empty() || line.front() == '[')
            continue;

        std::size_t split = line.find('=');
        if (split == std::string::npos)
            split = line.find_first_of(" \t");
        if (split == std::string::npos)
            continue;

        const std::string name = Trim(line.substr(0, split));
        std::string rawValue = Trim(line.substr(split + 1));
        if (name.empty())
            continue;

        switch (GetType(rawValue.c_str()))
        {
            case VALUE_TYPE_INT:
            {
                int value = 0;
                if (ParseInt(rawValue, value))
                    WriteIntValue(name.c_str(), value);
                break;
            }
            case VALUE_TYPE_FLOAT:
            {
                float value = 0.0f;
                if (ParseFloat(rawValue, value))
                    WriteFloatValue(name.c_str(), value);
                break;
            }
            default:
                WriteStringValue(name.c_str(), ParseString(rawValue).c_str());
                break;
        }
    }

    if (file.bad())
        return FALSE;

    file.close();
    const bool changed = ApplyPersistedDefaults();
    return !changed || Save();
}

BOOL CConfig::Save()
{
    return Save(m_szFilename) ? TRUE : FALSE;
}

bool CConfig::Load(const char* path)
{
    if (!path || !*path)
        return false;
    strncpy_s(m_szFilename, path, _TRUNCATE);
    return Load() != FALSE;
}

bool CConfig::Save(const char* path) const
{
    ZoneScoped;
    if (!path || !*path)
        return false;

    char absolute[MAX_PATH]{};
    const DWORD length = GetFullPathNameA(path, MAX_PATH, absolute, nullptr);
    if (!length || length >= MAX_PATH)
        return false;

    const char* slash = std::strrchr(absolute, '\\');
    if (!slash)
        return false;
    const std::string directory(absolute, static_cast<std::size_t>(slash - absolute + 1));

    char temporary[MAX_PATH]{};
    if (!GetTempFileNameA(directory.c_str(), "cfg", 0, temporary))
        return false;

    std::ofstream file(temporary, std::ios::trunc);
    if (!file.is_open())
    {
        DeleteFileA(temporary);
        return false;
    }

    for (int i = 0; i < MAX_ENTRIES; ++i)
    {
        if (!m_bNotEmpty[i])
            continue;
        file << m_entry[i].m_szName << '=' << Write(m_entry[i].m_szName) << '\n';
    }

    file.flush();
    const bool written = file.good();
    file.close();
    const bool saved = written && MoveFileExA(temporary, absolute, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    if (!saved)
        DeleteFileA(temporary);
    return saved;
}

const char* CConfig::ParamName(eParam param)
{
    return IsValidParam(param) ? kParamInfo[static_cast<std::size_t>(param)].name : nullptr;
}

bool CConfig::HasParam(eParam param) const
{
    const char* name = ParamName(param);
    return name && const_cast<CConfig*>(this)->DoesExist(name);
}

bool CConfig::GetParam(eParam param, int& value) const
{
    ZoneScoped;
    if (!IsValidParam(param))
        return false;
    const ParamInfo& info = kParamInfo[static_cast<std::size_t>(param)];
    if (info.type == PARAM_TYPE_STRING)
        return false;
    if (!HasParam(param))
    {
        value = info.type == PARAM_TYPE_BOOL ? (info.boolDefault ? 1 : 0) : info.integerDefault;
        return true;
    }
    value = const_cast<CConfig*>(this)->GetIntValue(info.name);
    return true;
}

bool CConfig::GetParam(eParam param, bool& value) const
{
    ZoneScoped;
    if (!IsValidParam(param))
        return false;
    const ParamInfo& info = kParamInfo[static_cast<std::size_t>(param)];
    if (info.type != PARAM_TYPE_BOOL)
        return false;
    if (!HasParam(param))
    {
        value = info.boolDefault;
        return true;
    }
    value = const_cast<CConfig*>(this)->GetIntValue(info.name) != 0;
    return true;
}

bool CConfig::GetParam(eParam param, std::string& value) const
{
    ZoneScoped;
    if (!IsValidParam(param))
        return false;
    const ParamInfo& info = kParamInfo[static_cast<std::size_t>(param)];
    if (info.type != PARAM_TYPE_STRING)
        return false;
    if (!HasParam(param))
    {
        value = info.stringDefault ? info.stringDefault : "";
        return true;
    }
    value = const_cast<CConfig*>(this)->GetStringValue(info.name);
    return true;
}

bool CConfig::SetParam(eParam param, int value)
{
    if (!IsValidParam(param))
        return false;
    const ParamInfo& info = kParamInfo[static_cast<std::size_t>(param)];
    if (info.type == PARAM_TYPE_STRING)
        return false;
    return WriteIntValue(info.name, info.type == PARAM_TYPE_BOOL ? (value != 0 ? 1 : 0) : value) != FALSE;
}

bool CConfig::SetParam(eParam param, bool value)
{
    if (!IsValidParam(param) || kParamInfo[static_cast<std::size_t>(param)].type != PARAM_TYPE_BOOL)
        return false;
    return WriteIntValue(ParamName(param), value ? 1 : 0) != FALSE;
}

bool CConfig::SetParam(eParam param, const char* value)
{
    if (!value || !IsValidParam(param) || kParamInfo[static_cast<std::size_t>(param)].type != PARAM_TYPE_STRING)
        return false;
    return WriteStringValue(ParamName(param), value) != FALSE;
}

bool CConfig::SetParam(eParam param, const std::string& value)
{
    return SetParam(param, value.c_str());
}

bool CConfig::ApplyPersistedDefaults()
{
    ZoneScoped;
    bool changed = false;

    if (!HasParam(PARAM_AUDIO_PROXY_OFF))
    {
        SetParam(PARAM_AUDIO_PROXY_OFF, false);
        changed = true;
    }

    int fpsLimit = 0;
    GetParam(PARAM_FPS_LIMIT, fpsLimit);
    if (!HasParam(PARAM_FPS_LIMIT) || fpsLimit < 20 || fpsLimit > 90)
    {
        SetParam(PARAM_FPS_LIMIT, 48);
        changed = true;
    }

    if (!HasParam(PARAM_MULTICORE))
    {
        SetParam(PARAM_MULTICORE, true);
        changed = true;
    }

    return changed;
}
