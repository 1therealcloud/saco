#pragma once

namespace ModelInfo
{
    constexpr int MAX_MODELS = 20000;

    // signed ids require the gta modelinfo relocation to be installed
    bool Valid(int modelId);
    void* GetRelocated(int modelId);
    bool IsReady(int modelId);
    bool IsOwnedClone(int modelId);
    int GetPedStat(int modelId);

    // clones start without rw objects, references or owned collision data
    void* ClonePed(int sourceModelId, int newModelId);
    void* CloneAtomic(int sourceModelId, int newModelId);

    // only removes a clone that has not acquired any game resources
    bool DiscardUnloadedClone(int modelId);
}
