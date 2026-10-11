#pragma once

#include <cstddef>
#include <cstdint>

#include <RakNetStatistics.h>

struct IDirect3DDevice9;

class CSrvNetStats
{
public:
    std::uint32_t m_dwLastTotalBytesSent; // 0x00
    std::uint32_t m_dwLastTotalBytesRecv; // 0x04
    std::uint32_t m_dwLastUpdateTick;     // 0x08
    std::uint32_t m_dwBPSUpload;          // 0x0C
    std::uint32_t m_dwBPSDownload;        // 0x10
    IDirect3DDevice9* m_pDevice;          // 0x14

    explicit CSrvNetStats(IDirect3DDevice9* device);

    void Draw();
    void SetActive(bool active);
    bool HasStatistics() const;

    static void Reset();
    static RakNetStatisticsStruct& RefStatistics();
    static void StoreStatistics(const RakNetStatisticsStruct& statistics);
};
