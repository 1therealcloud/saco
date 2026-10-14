/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * The original code is copyright (c) 2026, 1therealcloud.
 */

#include <stdafx.h>

#include "CDownloadedFilesPool.h"
#include "CCustomModelsPool.h"

DownloadedFileInfo* CDownloadedFilesPool::Find(DownloadedFileType type, std::uint32_t crc) const
{
    const auto key = (static_cast<std::uint64_t>(type) << 32) | crc;
    const auto found = m_byCrc.find(key);
    return found != m_byCrc.end() ? found->second : nullptr;
}
bool CDownloadedFilesPool::Queue(DownloadedFileType type, std::uint32_t crc, std::uint32_t size, std::uint32_t modelIndex)
{
    if ((type != DownloadedFileType::Dff && type != DownloadedFileType::Txd) || !size || size > CCustomModelsPool::MAX_FILE_SIZE || modelIndex >= CCustomModelsPool::MAX_MODELS)
        return false;
    if (const auto* file = Find(type, crc))
        return file->m_nFileSize == size;
    if (m_files.size() >= CCustomModelsPool::MAX_MODELS * 2u)
        return false;
    try
    {
        auto file = std::make_unique<DownloadedFileInfo>();
        file->m_nType = type;
        file->m_nExpectedCrc = crc;
        file->m_nFileSize = size;
        file->m_nModelIndex = modelIndex;
        file->m_bProgressDirty = true;
        const auto [it, inserted] = m_byCrc.emplace((static_cast<std::uint64_t>(type) << 32) | crc, file.get());
        if (!inserted)
            return false;
        try
        {
            m_files.push_back(std::move(file));
        }
        catch (...)
        {
            m_byCrc.erase(it);
            throw;
        }
        return true;
    }
    catch (const std::bad_alloc&)
    {
        return false;
    }
}
DownloadedFileInfo* CDownloadedFilesPool::GetNextQueued()
{
    while (m_nextQueued < m_files.size())
    {
        auto* file = m_files[m_nextQueued].get();
        if (file->m_state == DownloadState::Queued)
            return file;
        if (file->m_state != DownloadState::Complete && file->m_state != DownloadState::Failed)
            return nullptr;
        ++m_nextQueued;
    }
    return nullptr;
}

std::uint32_t CDownloadedFilesPool::GetCount() const
{
    return static_cast<std::uint32_t>(m_files.size());
}
void CDownloadedFilesPool::Clear()
{
    m_byCrc.clear();
    m_files.clear();
    m_nextQueued = 0;
}
DownloadedFileInfo* CDownloadedFilesPool::Get(std::uint32_t index) const
{
    return index < m_files.size() ? m_files[index].get() : nullptr;
}
