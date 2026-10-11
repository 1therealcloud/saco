#pragma once

#include <cstdint>

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
    std::uint8_t field_14[50]{};
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
    CCustomModelInfo** m_pObjects{};
    std::uint32_t m_nCount{};
};
