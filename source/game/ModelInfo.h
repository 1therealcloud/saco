/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * The original code is copyright (c) 2026, 1therealcloud.
 */

#pragma once

struct RpClump;

namespace ModelInfo
{
    constexpr int MAX_MODELS = 20000;

    // signed ids require the gta modelinfo relocation to be installed
    bool Valid(int modelId);
    void* GetRelocated(int modelId);
    bool IsReady(int modelId);
    bool IsOwnedClone(int modelId);
    int GetReferences(int modelId);
    int GetBaseModel(int modelId);
    bool AddReference(int modelId);
    void RemoveReference(int modelId);
    bool PinBaseModel(int modelId);
    bool SetTextureSlot(int modelId, int slot);
    int GetTextureSlot(int modelId);
    bool SetCollision(int modelId, void* collision);
    bool AttachPedClump(int modelId, RpClump* clump);
    bool DestroyClone(int modelId);
    int GetPedStat(int modelId);

    // clones start without rw objects, references or owned collision data
    void* ClonePed(int sourceModelId, int newModelId);
    void* CloneAtomic(int sourceModelId, int newModelId);

    // only removes a clone that has not acquired any game resources
    bool DiscardUnloadedClone(int modelId);
}
