#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#   define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#   define NOMINMAX
#endif

#include <Windows.h>

#include <cstddef>
#include <string>

class CConfig
{
public:
    enum
    {
        MAX_ENTRIES = 512,
        MAX_ENTRY_NAME = 40,
    };

    enum ValueType
    {
        VALUE_TYPE_NONE,
        VALUE_TYPE_INT,
        VALUE_TYPE_STRING,
        VALUE_TYPE_FLOAT
    };

    struct ConfigEntry
    {
        char m_szName[MAX_ENTRY_NAME + 1];
        BOOL m_bReadOnly;
        int m_nType;
        int m_nValue;
        float m_fValue;
        char* m_szValue;
    };

    enum eParam
    {
        PARAM_PAGE_SIZE,          // pagesize
        PARAM_FONT_SIZE,          // fontsize
        PARAM_TIMESTAMP,          // timestamp
        PARAM_DIRECT_MODE,        // directmode: retained for R5 cfg parity; no runtime effect
        PARAM_FPS_LIMIT,          // fpslimit
        PARAM_DISABLE_HEAD_MOVE,  // disableheadmove
        PARAM_MULTICORE,          // multicore
        PARAM_IME,                // ime
        PARAM_AUDIO_MESSAGES_OFF, // audiomsgoff
        PARAM_AUDIO_PROXY_OFF,    // audioproxyoff
        PARAM_NO_NAMETAG_STATUS,  // nonametagstatus
        PARAM_LOG_URLS,           // logurls
        PARAM_NO_HUD_SCALE_FIX,   // nohudscalefix
        PARAM_FONT_WEIGHT,        // fontweight
        PARAM_FONT_FACE,          // fontface
        PARAM_COUNT
    };

public:
    ConfigEntry m_entry[MAX_ENTRIES];
    BOOL m_bNotEmpty[MAX_ENTRIES];
    char m_szFilename[MAX_PATH + 1];
    int m_nFirstFree;

public:
    CConfig();
    explicit CConfig(const char* file);
    ~CConfig();

    CConfig(const CConfig&) = delete;
    CConfig& operator=(const CConfig&) = delete;

    // R5 low-level config API.
    void FindFirstFree();
    int GetIndex(const char* entry);
    bool DoesExist(const char* entry);
    int CreateEntry(const char* name);
    int GetIntValue(const char* entry);
    const char* GetStringValue(const char* entry);
    float GetFloatValue(const char* entry);
    BOOL Free(const char* entry);
    int GetValueType(const char* entry);
    ConfigEntry* GetEntry(int index);
    int GetType(const char* string);
    BOOL Save();
    BOOL WriteIntValue(const char* entry, int value, BOOL readOnly = FALSE);
    BOOL WriteStringValue(const char* entry, const char* value, BOOL readOnly = FALSE);
    BOOL WriteFloatValue(const char* entry, float value, BOOL readOnly = FALSE);
    void Write(const char* entry, char* buffer);
    BOOL Load();

    // Project adapter used by the rest of SACO.
    bool Load(const char* path);
    bool Save(const char* path) const;
    void Reset();

    std::string GetPath() const
    {
        return m_szFilename;
    }

    bool HasParam(eParam param) const;
    bool GetParam(eParam param, int& value) const;
    bool GetParam(eParam param, bool& value) const;
    bool GetParam(eParam param, std::string& value) const;
    bool SetParam(eParam param, int value);
    bool SetParam(eParam param, bool value);
    bool SetParam(eParam param, const char* value);
    bool SetParam(eParam param, const std::string& value);

private:
    static const char* ParamName(eParam param);
    bool ApplyPersistedDefaults();
    void ClearEntry(int index);
};
