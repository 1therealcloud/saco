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
#include <filesystem>
#include <vector>
#include <unordered_map>

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

enum class DownloadState
{
    Queued,
    Requested,
    Binary,
    Http,
    Complete,
    Failed
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
    std::filesystem::path m_tempPath{};
    DownloadState m_state{DownloadState::Queued};
    bool m_bProgressDirty{};
};

struct CDownloadedFilesPool
{
    DownloadedFileInfo* Find(DownloadedFileType type, std::uint32_t crc) const;
    bool Queue(DownloadedFileType type, std::uint32_t crc, std::uint32_t size, std::uint32_t modelIndex);
    DownloadedFileInfo* GetNextQueued();
    std::uint32_t GetCount() const;
    void Clear();
    DownloadedFileInfo* Get(std::uint32_t index) const;

private:
    std::vector<std::unique_ptr<DownloadedFileInfo>> m_files{};
    std::uint32_t m_nextQueued{};
    std::unordered_map<std::uint64_t, DownloadedFileInfo*> m_byCrc;
};
