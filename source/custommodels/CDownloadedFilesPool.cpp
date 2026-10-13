/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * The original code is copyright (c) 2026, 1therealcloud.
 */

#include <stdafx.h>

#include "CDownloadedFilesPool.h"

DownloadedFileInfo* CDownloadedFilesPool::Find(DownloadedFileType type, std::uint32_t crc) const
{
    const auto key = (static_cast<std::uint64_t>(type) << 32) | crc;
    const auto found = m_byCrc.find(key);
    return found != m_byCrc.end() ? found->second : nullptr;
}
bool CDownloadedFilesPool::Queue(DownloadedFileType type, std::uint32_t crc, std::uint32_t size, std::uint32_t modelIndex)
{
    if ((type != DownloadedFileType::Dff && type != DownloadedFileType::Txd) || !size)
        return false;
    if (const auto* file = Find(type, crc))
        return file->m_nFileSize == size;
    auto file = std::make_unique<DownloadedFileInfo>();
    file->m_nType = type;
    file->m_nExpectedCrc = crc;
    file->m_nFileSize = size;
    file->m_nModelIndex = modelIndex;
    auto* queued = file.get();
    m_files.push_back(std::move(file));
    m_byCrc.emplace((static_cast<std::uint64_t>(type) << 32) | crc, queued);
    return true;
}
std::uint32_t CDownloadedFilesPool::GetCount() const
{
    return static_cast<std::uint32_t>(m_files.size());
}
void CDownloadedFilesPool::Clear()
{
    m_byCrc.clear();
    m_files.clear();
}
DownloadedFileInfo* CDownloadedFilesPool::Get(std::uint32_t index) const
{
    return index < m_files.size() ? m_files[index].get() : nullptr;
}
