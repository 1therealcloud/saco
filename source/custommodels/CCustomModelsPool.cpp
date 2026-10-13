/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * The original code is copyright (c) 2026, 1therealcloud.
 */

#include <stdafx.h>

#include "CCustomModelsPool.h"

namespace
{
    bool SameMetadata(const CCustomModelInfo& a, const CCustomModelInfo& b)
    {
        return a.m_nType == b.m_nType && a.m_nVirtualWorld == b.m_nVirtualWorld && a.m_nStandardModelId == b.m_nStandardModelId && a.m_nCustomModelId == b.m_nCustomModelId &&
               a.m_nDffCrc == b.m_nDffCrc && a.m_nTxdCrc == b.m_nTxdCrc && a.m_nDffSize == b.m_nDffSize && a.m_nTxdSize == b.m_nTxdSize && a.m_nTimeOn == b.m_nTimeOn &&
               a.m_nTimeOff == b.m_nTimeOff;
    }
}

bool CCustomModelsPool::Set(std::uint32_t index, std::uint32_t count, const CCustomModelInfo& info)
{
    if (!count || count > MAX_MODELS || index >= count || count < m_models.size() || info.m_nStandardModelId < 0 || info.m_nStandardModelId >= 20000 || !info.m_nDffSize ||
        !info.m_nTxdSize || info.m_nDffSize > MAX_FILE_SIZE || info.m_nTxdSize > MAX_FILE_SIZE || info.m_nTimeOn > 23 || info.m_nTimeOff > 23)
        return false;
    if (info.m_nType == CustomModelType::Skin)
    {
        if (info.m_nCustomModelId < 20001 || info.m_nCustomModelId > 30000 || info.m_nStandardModelId > 311)
            return false;
    }
    else if (info.m_nType == CustomModelType::Object)
    {
        if (info.m_nCustomModelId > -1000 || info.m_nCustomModelId < -30000)
            return false;
    }
    else
        return false;
    auto* previous = Get(index);
    if (previous && SameMetadata(*previous, info))
    {
        if (count != m_models.size())
        {
            m_models.resize(count);
            ++m_revision;
        }
        return true;
    }
    if (previous && (previous->m_bLoaded || previous->m_bDffRequestPending || previous->m_bTxdRequestPending || previous->m_bDffQueued || previous->m_bTxdQueued))
        return false;
    auto model = std::make_unique<CCustomModelInfo>();
    model->m_nType = info.m_nType;
    model->m_nVirtualWorld = info.m_nVirtualWorld;
    model->m_nStandardModelId = info.m_nStandardModelId;
    model->m_nCustomModelId = info.m_nCustomModelId;
    model->m_nDffCrc = info.m_nDffCrc;
    model->m_nTxdCrc = info.m_nTxdCrc;
    model->m_nDffSize = info.m_nDffSize;
    model->m_nTxdSize = info.m_nTxdSize;
    model->m_nTimeOn = info.m_nTimeOn;
    model->m_nTimeOff = info.m_nTimeOff;
    m_models.resize(count);
    if (!previous)
        ++m_receivedCount;
    m_models[index] = std::move(model);
    ++m_revision;
    return true;
}
CCustomModelInfo* CCustomModelsPool::Get(std::uint32_t index) const
{
    return index < m_models.size() ? m_models[index].get() : nullptr;
}
std::uint32_t CCustomModelsPool::GetCount() const
{
    return static_cast<std::uint32_t>(m_models.size());
}
std::uint32_t CCustomModelsPool::GetReceivedCount() const
{
    return m_receivedCount;
}
bool CCustomModelsPool::IsReady() const
{
    return !m_models.empty() && m_receivedCount == m_models.size();
}
void CCustomModelsPool::Clear()
{
    m_models.clear();
    m_receivedCount = 0;
    ++m_revision;
}

std::uint64_t CCustomModelsPool::GetRevision() const
{
    return m_revision;
}
