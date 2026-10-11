#include <stdafx.h>

#include "patch.h"

namespace game::patch::modelinfo
{
    namespace
    {
        constexpr std::uint32_t kAtomicVanillaCapacity = 14000;
        constexpr std::uint32_t kAtomicSampCapacity = 20000;
        constexpr std::uint32_t kAtomicStore = 0x00AAE950;
        constexpr std::uintptr_t kAtomicCtor = 0x004C5540;

        constexpr std::uint32_t kPedVanillaCapacity = 278;
        constexpr std::uint32_t kPedSampCapacity = 319;
        constexpr std::uint32_t kPedStore = 0x00B478F8;
        constexpr std::uintptr_t kPedCtor = 0x004C57A0;

        constexpr std::uint32_t kTxdVanilla = 5000;
        constexpr std::uint32_t kTxdSamp = 20000;

        constexpr std::uint8_t kVehicleStructVanilla = 50;
        constexpr std::uint8_t kVehicleStructSamp = 127;

        // read the active table from gta code so limit adjusters can relocate it
        constexpr std::uintptr_t kModelInfoPtrsInstruction = 0x00403DA4;
        constexpr std::uintptr_t kModelInfoPtrsOperand = kModelInfoPtrsInstruction + 3;

        struct AtomicModelInfo
        {
            std::array<std::uint8_t, 0x20> bytes;
        };

        struct PedModelInfo
        {
            std::array<std::uint8_t, 0x44> bytes;
        };

        static_assert(sizeof(AtomicModelInfo) == 0x20);
        static_assert(sizeof(PedModelInfo) == 0x44);

        template <typename T, std::size_t N>
        struct Store
        {
            std::uint32_t count;
            std::array<T, N> objects;
        };

        using AtomicStoreLayout = Store<AtomicModelInfo, 1>;
        using PedStoreLayout = Store<PedModelInfo, 1>;

        static_assert(offsetof(AtomicStoreLayout, objects) == 4);
        static_assert(offsetof(PedStoreLayout, objects) == 4);

        alignas(16) Store<AtomicModelInfo, kAtomicSampCapacity> g_atomicStore{};
        alignas(16) Store<PedModelInfo, kPedSampCapacity> g_pedStore{};

        struct PointerSite
        {
            std::uintptr_t address;
            std::uint32_t displacement;
        };

        // ola atomic store refs; displacement is from the CStore base
        constexpr std::array<PointerSite, 35> kAtomicSites = {
            PointerSite{0x004C63E1, 0x00}, PointerSite{0x004C63FE, 0x00}, PointerSite{0x004C65DA, 0x00}, PointerSite{0x004C6621, 0x00}, PointerSite{0x004C6633, 0x00},
            PointerSite{0x004C6865, 0x00}, PointerSite{0x004C689A, 0x00}, PointerSite{0x004C68B5, 0x00}, PointerSite{0x004C68E8, 0x00}, PointerSite{0x004C68F9, 0x00},
            PointerSite{0x004C6927, 0x00}, PointerSite{0x004C6938, 0x00}, PointerSite{0x004C6966, 0x00}, PointerSite{0x004C6977, 0x00}, PointerSite{0x004C69A5, 0x00},
            PointerSite{0x004C69B6, 0x00}, PointerSite{0x004C69E4, 0x00}, PointerSite{0x004C69F5, 0x00}, PointerSite{0x004C6A23, 0x00}, PointerSite{0x004C6A34, 0x00},

            PointerSite{0x004C63F2, 0x04}, PointerSite{0x004C662D, 0x04}, PointerSite{0x004C6822, 0x04}, PointerSite{0x004C6829, 0x04}, PointerSite{0x004C6877, 0x04},
            PointerSite{0x004C6881, 0x04}, PointerSite{0x004C6890, 0x04}, PointerSite{0x004C68A5, 0x04}, PointerSite{0x004C68F3, 0x04}, PointerSite{0x004C6932, 0x04},
            PointerSite{0x004C6971, 0x04}, PointerSite{0x004C69B0, 0x04}, PointerSite{0x004C69EF, 0x04}, PointerSite{0x004C6A2E, 0x04},

            PointerSite{0x004C68AC, 0x1C},
        };

        // ola ped store refs
        constexpr std::array<PointerSite, 8> kPedSites = {
            PointerSite{0x004C6518, 0x00}, PointerSite{0x004C652F, 0x00}, PointerSite{0x004C660A, 0x00}, PointerSite{0x004C67A1, 0x00},
            PointerSite{0x004C67B3, 0x00}, PointerSite{0x004C6859, 0x00}, PointerSite{0x004C6523, 0x04}, PointerSite{0x004C67AD, 0x04},
        };

        using AtomicCtor = void(__thiscall*)(AtomicModelInfo*);
        using PedCtor = void(__thiscall*)(PedModelInfo*);

        template <std::size_t N>
        bool resolve_store_base(const std::array<PointerSite, N>& sites, std::uint32_t& base)
        {
            base = 0;

            for (const auto& site : sites)
            {
                std::uint32_t value{};

                if (!::patch::read(site.address, &value, sizeof(value)))
                    return false;

                if (value < site.displacement)
                    return false;

                const auto candidate = value - site.displacement;

                if (!base)
                    base = candidate;
                else if (base != candidate)
                    return false;
            }

            return base != 0;
        }

        template <std::size_t N>
        bool relocate_store(const std::array<PointerSite, N>& sites, std::uint32_t newBase)
        {
            for (const auto& site : sites)
            {
                if (!::patch::write(site.address, newBase + site.displacement))
                    return false;
            }

            return true;
        }

        std::uint32_t active_model_info_ptrs()
        {
            // mov eax, [model*4 + ms_modelInfoPtrs]
            constexpr std::array<std::uint8_t, 3> prefix = {0x8B, 0x04, 0x85};

            if (!::patch::expect(kModelInfoPtrsInstruction, prefix.data(), prefix.size()))
                return 0;

            std::uint32_t ptr{};
            if (!::patch::read(kModelInfoPtrsOperand, &ptr, sizeof(ptr)))
                return 0;

            return ptr;
        }

        bool install_atomic_store()
        {
            std::uint32_t activeStore{};
            if (!resolve_store_base(kAtomicSites, activeStore))
                return false;

            // another limit adjuster already owns this store
            if (activeStore != kAtomicStore)
                return true;

            std::uint32_t vanillaCount{};
            if (!::patch::read(kAtomicStore, &vanillaCount, sizeof(vanillaCount)))
                return false;

            // moving a live store would leave stale model pointers
            if (vanillaCount != 0)
                return false;

            const auto ctor = reinterpret_cast<AtomicCtor>(kAtomicCtor);
            g_atomicStore.count = 0;

            for (auto& model : g_atomicStore.objects)
                ctor(&model);

            const auto newBase = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&g_atomicStore));

            if (!relocate_store(kAtomicSites, newBase))
                return false;

            return true;
        }

        bool install_ped_store()
        {
            std::uint32_t activeStore{};
            if (!resolve_store_base(kPedSites, activeStore))
                return false;

            if (activeStore != kPedStore)
                return true;

            std::uint32_t vanillaCount{};
            if (!::patch::read(kPedStore, &vanillaCount, sizeof(vanillaCount)))
                return false;

            if (vanillaCount != 0)
                return false;

            const auto ctor = reinterpret_cast<PedCtor>(kPedCtor);
            g_pedStore.count = 0;

            for (auto& model : g_pedStore.objects)
                ctor(&model);

            const auto newBase = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&g_pedStore));

            if (!relocate_store(kPedSites, newBase))
                return false;

            return true;
        }

        bool patch_txd_capacity()
        {
            std::uint32_t value{};
            if (!::patch::read(address::txdCapacity, &value, sizeof(value)))
                return false;

            if (value == kTxdVanilla)
                return ::patch::write(address::txdCapacity, kTxdSamp);

            return true;
        }

        bool patch_vehicle_struct_capacity()
        {
            std::uint8_t value{};
            if (!::patch::read(address::vehicleStructCount, &value, sizeof(value)))
                return false;

            if (value == kVehicleStructVanilla)
                return ::patch::write(address::vehicleStructCount, kVehicleStructSamp);

            return true;
        }
    }

    bool install()
    {
        const auto modelInfoPtrs = active_model_info_ptrs();
        if (!modelInfoPtrs)
            return false;

        if (!install_atomic_store())
            return false;

        if (!install_ped_store())
            return false;

        if (!patch_txd_capacity())
            return false;

        if (!patch_vehicle_struct_capacity())
            return false;

        return true;
    }
}
