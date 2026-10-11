#include <stdafx.h>

#include "ModelInfo.h"
#include <cstdlib>

namespace ModelInfo
{
    namespace
    {
        bool Valid(int model)
        {
            return model >= 0 && model < MAX_MODELS;
        }
        void** Table()
        {
            // read the active operand, including tables moved by limit adjusters
            void** table = nullptr;
            std::memcpy(&table, reinterpret_cast<const void*>(0x403DA7), sizeof(table));
            return table;
        }
        std::size_t AtomicSize(void* model)
        {
            if (!model)
                return 0;
            switch (*static_cast<const std::uintptr_t*>(model))
            {
                case 0x85BBF0:
                    return 0x20; // CAtomicModelInfo
                case 0x85BC30:
                    return 0x24; // CDamageAtomicModelInfo
                case 0x85BC70:
                    return 0x24; // CLodAtomicModelInfo
                case 0x85BCB0:
                    return 0x24; // CTimeModelInfo
                case 0x85BCF0:
                    return 0x28; // CLodTimeModelInfo
                default:
                    return 0;
            }
        }
        void* Copy(void** table, void* source, int destination, std::size_t size)
        {
            // loaded models own RW objects and TXD references; copying those would cause double destruction
            std::uint16_t references = 0;
            void* rwObject = nullptr;
            std::memcpy(&references, static_cast<const std::uint8_t*>(source) + 0x08, sizeof(references));
            std::memcpy(&rwObject, static_cast<const std::uint8_t*>(source) + 0x1C, sizeof(rwObject));
            if (references || rwObject)
                return nullptr;
            void* copy = std::malloc(size);
            if (!copy)
                return nullptr;
            std::memcpy(copy, source, size);
            table[destination] = copy;
            return copy;
        }
    }

    void* GetRelocated(int modelId)
    {
        if (!Valid(modelId))
            return nullptr;
        auto** table = Table();
        return table ? table[modelId] : nullptr;
    }

    int GetPedStat(int modelId)
    {
        const auto* info = static_cast<const std::uint8_t*>(GetRelocated(modelId));
        if (!info || *reinterpret_cast<const std::uintptr_t*>(info) != 0x85BDC0)
            return 0;
        // CPedModelInfo::m_nStatType, after the anim group and ped type
        int stat = 0;
        std::memcpy(&stat, info + 0x2C, sizeof(stat));
        return stat;
    }

    void* ClonePed(int sourceModelId, int newModelId)
    {
        if (!Valid(sourceModelId) || !Valid(newModelId))
            return nullptr;
        auto** table = Table();
        if (!table || table[newModelId])
            return nullptr;
        void* source = table[sourceModelId];
        if (!source || *static_cast<const std::uintptr_t*>(source) != 0x85BDC0)
            return nullptr;
        return Copy(table, source, newModelId, 0x44);
    }

    void* CloneAtomic(int sourceModelId, int newModelId)
    {
        if (!Valid(sourceModelId) || !Valid(newModelId))
            return nullptr;
        auto** table = Table();
        if (!table)
            return nullptr;
        if (table[newModelId])
            return table[newModelId];
        void* source = table[sourceModelId];
        const auto size = AtomicSize(source);
        return size ? Copy(table, source, newModelId, size) : nullptr;
    }
}
