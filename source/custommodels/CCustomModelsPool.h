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
    // keep empty slots so rpc model indices stay stable
    std::vector<std::unique_ptr<CCustomModelInfo>> m_models{};
};
