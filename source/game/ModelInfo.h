#pragma once

namespace ModelInfo
{
    constexpr int MAX_MODELS = 20000;

    // use on the game thread after GTA initializes its model table
    void* GetRelocated(int modelId);
    int GetPedStat(int modelId);
    // copies require an unloaded, unreferenced source; collision metadata remains shared
    // newModelId must be an unused slot; GTA keeps the metadata for the process lifetime
    void* ClonePed(int sourceModelId, int newModelId);
    // returns the existing entry if the destination slot is already occupied
    void* CloneAtomic(int sourceModelId, int newModelId);
}
