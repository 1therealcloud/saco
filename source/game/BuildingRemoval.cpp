#include <stdafx.h>
#include "BuildingRemoval.h"
#include "utils/math/CVector.h"
#include <cmath>

namespace BuildingRemoval
{
    namespace
    {
        struct Removal
        {
            int model;
            CVector position;
            float radius;
        };
        std::array<Removal, MAX_REMOVALS> removals{};
        std::size_t count = 0;
        struct Instance
        {
            CVector position;
            float quaternion[4];
            int model;
            std::uint32_t flags;
            int lod;
        };
        template <class T>
        T Read(const void* data, std::size_t offset)
        {
            T value;
            std::memcpy(&value, static_cast<const std::uint8_t*>(data) + offset, sizeof(value));
            return value;
        }
        template <class T>
        void Write(void* data, std::size_t offset, const T& value)
        {
            std::memcpy(static_cast<std::uint8_t*>(data) + offset, &value, sizeof(value));
        }
        struct EntityKey
        {
            std::uintptr_t poolAddress;
            void* pool;
            std::uint8_t* storage;
            const std::uint8_t* states;
            int slot;
            std::size_t stride;
            std::uint8_t generation;
            void* Entity() const
            {
                return storage + stride * slot;
            }
            bool Alive() const
            {
                if (*reinterpret_cast<void**>(poolAddress) != pool)
                    return false;
                return Read<void*>(pool, 0) == storage && Read<const void*>(pool, 4) == states && slot < Read<int>(pool, 8) && states[slot] == generation;
            }
        };
        struct SavedEntity
        {
            EntityKey key;
            CVector position;
            float placementZ;
            std::uint8_t area;
        };
        struct SavedOccluder
        {
            std::uint32_t index;
            std::array<std::uint8_t, 12> geometry;
        };
        std::unordered_map<void*, SavedEntity> hidden;
        std::vector<EntityKey> pending;
        std::vector<SavedOccluder> occluders;
        bool Finite(CVector p)
        {
            return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
        }
        bool Inside(CVector p, CVector c, float radius)
        {
            return (p - c).Magnitude() <= radius;
        }
        CVector Position(void* entity)
        {
            auto* matrix = Read<void*>(entity, 0x14);
            return matrix ? Read<CVector>(matrix, 0x30) : Read<CVector>(entity, 4);
        }
        bool FindKey(void* entity, EntityKey& key)
        {
            ZoneScoped;
            const std::pair<std::uintptr_t, std::size_t> pools[] = {{0xB74498, 0x38}, {0xB744A0, 0x38}, {0xB7449C, 0x19C}};
            for (const auto& [address, stride] : pools)
            {
                auto* pool = *reinterpret_cast<void**>(address);
                if (!pool)
                    continue;
                auto* storage = Read<std::uint8_t*>(pool, 0);
                const auto* states = Read<const std::uint8_t*>(pool, 4);
                const int capacity = Read<int>(pool, 8);
                const auto ptr = reinterpret_cast<std::uintptr_t>(entity), base = reinterpret_cast<std::uintptr_t>(storage);
                if (!storage || !states || capacity <= 0 || ptr < base || ptr - base >= stride * capacity || (ptr - base) % stride)
                    continue;
                const int slot = static_cast<int>((ptr - base) / stride);
                if (states[slot] & 0x80)
                    return false;
                key = {address, pool, storage, states, slot, stride, states[slot]};
                return true;
            }
            return false;
        }
        void HideOnce(const EntityKey& key)
        {
            ZoneScoped;
            if (!key.Alive())
                return;
            auto* entity = key.Entity();
            if (hidden.contains(entity))
                return;
            hidden.emplace(entity, SavedEntity{key, Position(entity), Read<float>(entity, 0x0C), Read<std::uint8_t>(entity, 0x2F)});
            Write(entity, 0x2F, std::uint8_t{1});
            Write(entity, 0x0C, Read<float>(entity, 0x0C) - 2000.0f);
            if (auto* matrix = Read<void*>(entity, 0x14))
                Write(matrix, 0x38, Read<float>(matrix, 0x38) - 2000.0f);
            reinterpret_cast<void(__thiscall*)(void*)>(0x446F90)(entity); // update current rw matrix
            reinterpret_cast<void(__thiscall*)(void*)>(0x532B00)(entity);
        }
        void RemoveFromPool(std::uintptr_t address, std::size_t stride, const Removal& removal)
        {
            ZoneScoped;
            auto* pool = *reinterpret_cast<void**>(address);
            if (!pool)
                return;
            auto* storage = Read<std::uint8_t*>(pool, 0);
            const auto* states = Read<const std::uint8_t*>(pool, 4);
            const int capacity = Read<int>(pool, 8);
            if (!storage || !states)
                return;
            for (int i = 0; i < capacity; ++i)
            {
                if (states[i] & 0x80)
                    continue;
                auto* entity = storage + stride * i;
                const auto vtable = Read<std::uintptr_t>(entity, 0);
                if (!vtable || vtable == 0x863C40)
                    continue;
                if (hidden.contains(entity))
                    continue;
                const int model = Read<std::uint16_t>(entity, 0x22);
                if ((removal.model == -1 || removal.model == model) && Inside(Position(entity), removal.position, removal.radius))
                    HideOnce({address, pool, storage, states, i, stride, states[i]});
            }
        }
        void RemoveOccluders(CVector center)
        {
            ZoneScoped;
            const auto total = std::min(*reinterpret_cast<const std::uint32_t*>(0xC73F98), 1000u);
            auto* data = reinterpret_cast<std::uint8_t*>(0xC73FA0);
            for (std::uint32_t i = 0; i < total; ++i)
            {
                if (std::any_of(occluders.begin(), occluders.end(), [i](const auto& saved) {
                        return saved.index == i;
                    }))
                    continue;
                auto* occluder = data + 0x12 * i;
                const CVector position{Read<std::int16_t>(occluder, 0) * 0.25f, Read<std::int16_t>(occluder, 2) * 0.25f, Read<std::int16_t>(occluder, 4) * 0.25f};
                if ((position - center).Magnitude() < 500.0f)
                {
                    SavedOccluder saved{i, {}};
                    std::memcpy(saved.geometry.data(), occluder, 12);
                    occluders.push_back(saved);
                    std::memset(occluder, 0, 12);
                }
            }
            *reinterpret_cast<std::uint32_t*>(0xC73CC0) = 0;
        }
    }
    namespace detail
    {
        void QueueLoadedInstance(void* entity, const void* data)
        {
            const auto* instance = static_cast<const Instance*>(data);
            EntityKey key{};
            if (entity && instance && ShouldRemoveBuilding(instance->model, instance->position.x, instance->position.y, instance->position.z) && FindKey(entity, key))
                pending.push_back(key);
        }
        void ForgetEntity(void* entity)
        {
            hidden.erase(entity);
            std::erase_if(pending, [entity](const auto& key) {
                return key.Entity() == entity;
            });
        }
        void ClearSnapshots()
        {
            hidden.clear();
            pending.clear();
            occluders.clear();
            count = 0;
        }
    }
    bool ShouldRemoveBuilding(int model, float x, float y, float z)
    {
        for (std::size_t i = 0; i < count; ++i)
            if ((removals[i].model == -1 || removals[i].model == model) && Inside({x, y, z}, removals[i].position, removals[i].radius))
                return true;
        return false;
    }
    void Process()
    {
        ZoneScoped;

        for (const auto& key : pending)
            if (key.Alive())
            {
                const auto p = Position(key.Entity());
                if (ShouldRemoveBuilding(Read<std::uint16_t>(key.Entity(), 0x22), p.x, p.y, p.z))
                    HideOnce(key);
            }
        pending.clear();
    }
    bool RemoveBuildingsInArea(int model, float x, float y, float z, float radius)
    {
        ZoneScoped;
        if (model < -1 || model > 65535 || !Finite({x, y, z}) || !std::isfinite(radius) || radius < 0)
            return false;
        for (std::size_t i = 0; i < count; ++i)
            if (removals[i].model == model && removals[i].position.x == x && removals[i].position.y == y && removals[i].position.z == z && removals[i].radius == radius)
                return true;
        if (count == MAX_REMOVALS)
            return false;
        const Removal removal{model, {x, y, z}, radius};
        removals[count++] = removal;
        RemoveOccluders(removal.position);
        RemoveFromPool(0xB74498, 0x38, removal);
        RemoveFromPool(0xB744A0, 0x38, removal);
        RemoveFromPool(0xB7449C, 0x19C, removal);
        return true;
    }
    void RestoreAll()
    {
        ZoneScoped;
        count = 0;
        pending.clear();
        std::size_t restored = 0;
        for (const auto& [entity, saved] : hidden)
        {
            if (!saved.key.Alive())
                continue;
            Write(entity, 0x2F, saved.area);
            Write(entity, 0x0C, saved.placementZ);
            if (auto* matrix = Read<void*>(entity, 0x14))
                Write(matrix, 0x38, saved.position.z);
            else
                Write(entity, 0x0C, saved.position.z);
            reinterpret_cast<void(__thiscall*)(void*)>(0x446F90)(entity);
            reinterpret_cast<void(__thiscall*)(void*)>(0x532B00)(entity);
            ++restored;
        }
        const auto total = std::min(*reinterpret_cast<const std::uint32_t*>(0xC73F98), 1000u);
        for (const auto& saved : occluders)
            if (saved.index < total)
                std::memcpy(reinterpret_cast<std::uint8_t*>(0xC73FA0) + 0x12 * saved.index, saved.geometry.data(), 12);
        *reinterpret_cast<std::uint32_t*>(0xC73CC0) = 0;
        hidden.clear();
        occluders.clear();
    }
    void Reset()
    {
        RestoreAll();
    }

}
