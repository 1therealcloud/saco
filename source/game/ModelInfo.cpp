/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * The original code is copyright (c) 2026, 1therealcloud.
 */

#include <stdafx.h>

#include "ModelInfo.h"
#include "game/patch/patch.h"

#include <cstdlib>

namespace ModelInfo
{
    namespace
    {
        constexpr std::uintptr_t kPedVtable = 0x85BDC0;
        constexpr std::uintptr_t kAtomicVtable = 0x85BBF0;
        constexpr std::uintptr_t kDamageAtomicVtable = 0x85BC30;
        constexpr std::uintptr_t kLodAtomicVtable = 0x85BC70;
        constexpr std::uintptr_t kTimeVtable = 0x85BCB0;
        constexpr std::uintptr_t kLodTimeVtable = 0x85BCF0;

        constexpr std::size_t kReferencesOffset = 0x08;
        constexpr std::size_t kTxdSlotOffset = 0x0A;
        constexpr std::size_t kFlagsOffset = 0x12;
        constexpr std::size_t kCollisionOffset = 0x14;
        constexpr std::size_t kRwObjectOffset = 0x1C;
        constexpr std::size_t kDamagedAtomicOffset = 0x20;
        constexpr std::size_t kHitCollisionOffset = 0x34;

        static_assert(sizeof(void*) == 4, "GTA:SA model infos require x86");

        struct OwnedClone
        {
            void* info{};
            std::uint32_t borrowedCollision{};
            std::int16_t inheritedTxdSlot{};
            bool ped{};
            bool damageAtomic{};
            int baseModelId{};
        };

        // entries live until explicitly discarded, not until static destruction
        std::unordered_map<int, OwnedClone> g_ownedClones;

        template <typename T>
        T Read(const void* info, std::size_t offset)
        {
            T value{};
            std::memcpy(&value, static_cast<const std::uint8_t*>(info) + offset, sizeof(value));
            return value;
        }

        template <typename T>
        void Write(void* info, std::size_t offset, T value)
        {
            std::memcpy(static_cast<std::uint8_t*>(info) + offset, &value, sizeof(value));
        }

        std::size_t AtomicSize(const void* model)
        {
            ZoneScoped;
            if (!model)
                return 0;

            switch (Read<std::uint32_t>(model, 0))
            {
                case kAtomicVtable:
                    return 0x20;
                case kDamageAtomicVtable:
                    return 0x24;
                case kLodAtomicVtable:
                    return 0x24;
                case kTimeVtable:
                    return 0x24;
                case kLodTimeVtable:
                    return 0x28;
                default:
                    return 0;
            }
        }

        void* Copy(void** table, const void* source, int sourceModelId, int destination, std::size_t size, bool ped)
        {
            ZoneScoped;
            if (table[destination] || g_ownedClones.contains(destination))
                return nullptr;

            auto* copy = static_cast<std::uint8_t*>(std::malloc(size));
            if (!copy)
                return nullptr;

            std::memcpy(copy, source, size);
            const bool damageAtomic = !ped && Read<std::uint32_t>(source, 0) == kDamageAtomicVtable;

            // a clone borrows static model metadata, not live rw resources
            Write<std::uint16_t>(copy, kReferencesOffset, 0);
            Write<std::uint32_t>(copy, kRwObjectOffset, 0);
            if (ped)
                Write<std::uint32_t>(copy, kHitCollisionOffset, 0);
            if (damageAtomic)
                Write<std::uint32_t>(copy, kDamagedAtomicOffset, 0);

            // preserve lod and rendering flags, but not collision ownership
            auto flags = Read<std::uint8_t>(copy, kFlagsOffset);
            flags &= static_cast<std::uint8_t>(~0x21u);
            Write(copy, kFlagsOffset, flags);
            if (ped)
            {
                // clump collision ownership is in the second flags byte
                auto clumpFlags = Read<std::uint8_t>(copy, kFlagsOffset + 1);
                clumpFlags &= static_cast<std::uint8_t>(~0x08u);
                Write(copy, kFlagsOffset + 1, clumpFlags);
            }

            const OwnedClone record{copy, Read<std::uint32_t>(copy, kCollisionOffset), Read<std::int16_t>(copy, kTxdSlotOffset), ped, damageAtomic, sourceModelId};
            try
            {
                if (!g_ownedClones.emplace(destination, record).second)
                {
                    std::free(copy);
                    return nullptr;
                }
            }
            catch (...)
            {
                std::free(copy);
                return nullptr;
            }

            table[destination] = copy;
            return copy;
        }
    }

    bool Valid(int modelId)
    {
        return game::patch::modelinfo::valid_id(modelId);
    }

    void* GetRelocated(int modelId)
    {
        if (!Valid(modelId))
            return nullptr;

        auto** table = game::patch::modelinfo::active_table();
        return table ? table[modelId] : nullptr;
    }

    bool IsReady(int modelId)
    {
        const auto* info = GetRelocated(modelId);
        return info && Read<std::uint32_t>(info, kRwObjectOffset) != 0;
    }

    bool IsOwnedClone(int modelId)
    {
        const auto it = g_ownedClones.find(modelId);
        return it != g_ownedClones.end() && GetRelocated(modelId) == it->second.info;
    }

    int GetPedStat(int modelId)
    {
        const auto* info = GetRelocated(modelId);
        if (!info || Read<std::uint32_t>(info, 0) != kPedVtable)
            return 0;

        return Read<int>(info, 0x2C);
    }

    void* ClonePed(int sourceModelId, int newModelId)
    {
        ZoneScoped;
        if (sourceModelId < 0 || !Valid(sourceModelId) || !Valid(newModelId) || sourceModelId == newModelId)
            return nullptr;

        auto** table = game::patch::modelinfo::active_table();
        if (!table || table[newModelId])
            return nullptr;

        const auto* source = table[sourceModelId];
        if (!source || Read<std::uint32_t>(source, 0) != kPedVtable)
            return nullptr;

        return Copy(table, source, sourceModelId, newModelId, 0x44, true);
    }

    void* CloneAtomic(int sourceModelId, int newModelId)
    {
        ZoneScoped;
        if (sourceModelId < 0 || !Valid(sourceModelId) || !Valid(newModelId) || sourceModelId == newModelId)
            return nullptr;

        auto** table = game::patch::modelinfo::active_table();
        if (!table)
            return nullptr;

        if (table[newModelId])
        {
            const auto it = g_ownedClones.find(newModelId);
            return it != g_ownedClones.end() && it->second.info == table[newModelId] && !it->second.ped && it->second.baseModelId == sourceModelId ? table[newModelId] : nullptr;
        }

        const auto* source = table[sourceModelId];
        const auto size = AtomicSize(source);
        return size ? Copy(table, source, sourceModelId, newModelId, size, false) : nullptr;
    }

    bool DiscardUnloadedClone(int modelId)
    {
        ZoneScoped;
        if (!IsOwnedClone(modelId))
            return false;

        const auto it = g_ownedClones.find(modelId);
        const auto& record = it->second;
        const auto* info = record.info;
        const auto flags = Read<std::uint8_t>(info, kFlagsOffset);
        const auto clumpFlags = Read<std::uint8_t>(info, kFlagsOffset + 1);
        if (Read<std::uint16_t>(info, kReferencesOffset) || Read<std::uint32_t>(info, kRwObjectOffset) || Read<std::int16_t>(info, kTxdSlotOffset) != record.inheritedTxdSlot ||
            Read<std::uint32_t>(info, kCollisionOffset) != record.borrowedCollision || (flags & 0x20u) ||
            (record.ped && (Read<std::uint32_t>(info, kHitCollisionOffset) || (clumpFlags & 0x08u))) || (record.damageAtomic && Read<std::uint32_t>(info, kDamagedAtomicOffset)))
            return false;

        auto** table = game::patch::modelinfo::active_table();
        table[modelId] = nullptr;
        g_ownedClones.erase(it);
        std::free(const_cast<void*>(info));
        return true;
    }
}
