#pragma once

#include <cstddef>

namespace BuildingRemoval
{
    constexpr std::size_t MAX_REMOVALS = 1000;

    // game thread only; model -1 matches every model
    bool ShouldRemoveBuilding(int modelId, float x, float y, float z);                // 0x9D050
    bool RemoveBuildingsInArea(int modelId, float x, float y, float z, float radius); // 0x9D3D0
    // apply streamed instances after gta finishes IPL loading, on the game thread
    void Process();
    void RestoreAll();
    // restore the map and clear session rules
    void Reset();
}

namespace BuildingRemoval::detail
{
    void QueueLoadedInstance(void* entity, const void* instance);
    void ForgetEntity(void* entity);
    void ClearSnapshots();
}
