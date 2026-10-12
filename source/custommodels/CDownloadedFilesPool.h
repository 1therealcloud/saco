/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * The original code is copyright (c) 2026, 1therealcloud.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

enum class DownloadedFileType : std::uint8_t
{
    Dff = 1,
    Txd = 2,
};

struct DownloadedFileCloser
{
    void operator()(std::FILE* file) const noexcept
    {
        if (file)
            std::fclose(file);
    }
};

struct DownloadedFileInfo
{
    std::unique_ptr<std::FILE, DownloadedFileCloser> m_pFile{};
    DownloadedFileType m_nType{};
    std::uint32_t m_nDownloadedSize{};
    std::uint32_t m_nFileSize{};
    std::uint32_t m_nRunningCrc{};
    std::uint32_t m_nExpectedCrc{};
    std::uint32_t m_nModelIndex{};
    std::string m_tempPath{};
};

struct DownloadSlot
{
    std::uint32_t m_nId{};
    bool m_bCompleted{};
    bool m_bActive{};
    bool m_bValid{};
    bool m_bInitialized{};
};

struct CDownloadedFilesPool
{
    std::vector<std::unique_ptr<DownloadedFileInfo>> m_files{};
};
