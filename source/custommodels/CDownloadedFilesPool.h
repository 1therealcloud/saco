#pragma once

#include <cstddef>
#include <cstdint>

#include "CCustomModelsPool.h"

enum class DownloadedFileType : std::uint8_t
{
    Dff = 1,
    Txd = 2,
};

struct DownloadedFileInfo
{
    void* m_pFile{};
    DownloadedFileType m_nType{};
    std::uint32_t m_nDownloadedSize{};
    std::uint32_t m_nFileSize{};
    std::uint32_t m_nRunningCrc{};
    std::uint32_t m_nExpectedCrc{};
    std::uint32_t m_nModelIndex{};
    char m_szTempPath[261]{};
};

struct DownloadSlot
{
    std::uint32_t m_nId{};
    bool m_bCompleted{};
    bool m_bActive{};
    bool m_bValid{};
    bool m_bInitialized{};
    std::uint8_t m_data[552]{};
};

struct CDownloadedFilesPool
{
    DownloadedFileInfo** m_pObjects{};
    std::uint32_t m_nCount{};
};
