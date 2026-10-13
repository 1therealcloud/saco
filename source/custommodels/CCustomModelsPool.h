/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * The original code is copyright (c) 2026, 1therealcloud.
 */

#pragma once

#include <cstdint>
#include <memory>
#include <vector>

enum class CustomModelType : std::uint8_t
{
    Skin = 1,
    Object = 2,
};

struct CCustomModelInfo
{
    bool m_bDffExists{};
    bool m_bTxdExists{};
    bool m_bDffQueued{};
    bool m_bTxdQueued{};
    bool m_bDffRequestPending{};
    bool m_bTxdRequestPending{};
    bool m_bDownloadFailed{};

    CustomModelType m_nType{};
    std::int32_t m_nVirtualWorld{};
    std::int32_t m_nStandardModelId{};
    std::int32_t m_nCustomModelId{};

    std::uint32_t m_nDffCrc{};
    std::uint32_t m_nTxdCrc{};
    std::uint32_t m_nDffSize{};
    std::uint32_t m_nTxdSize{};
    std::uint8_t m_nTimeOn{};
    std::uint8_t m_nTimeOff{};

    bool m_bLoaded{};
    bool m_bPendingUnload{};
    std::uint32_t m_nUnloadCounter{};
};

struct CCustomModelsPool
{
    static constexpr std::uint32_t MAX_MODELS = 65536;
    static constexpr std::uint32_t MAX_FILE_SIZE = 64u * 1024u * 1024u;
    bool Set(std::uint32_t index, std::uint32_t count, const CCustomModelInfo& info);
    CCustomModelInfo* Get(std::uint32_t index) const;
    std::uint32_t GetCount() const;
    std::uint32_t GetReceivedCount() const;
    bool IsReady() const;
    std::uint64_t GetRevision() const;
    void Clear();

private:
    // Empty slots preserve server indices while metadata arrives out of order.
    std::vector<std::unique_ptr<CCustomModelInfo>> m_models{};
    std::uint32_t m_receivedCount{};
    std::uint64_t m_revision{};
};
