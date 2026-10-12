#include <stdafx.h>

#include "patch.h"
#include "hooks.h"
#include "modelinfo_sites.h"

namespace game::patch::modelinfo
{
    namespace
    {
        constexpr std::int32_t kSignedMinId = -32767;
        constexpr std::int32_t kSignedMaxId = 32767;
        constexpr std::size_t kSignedPivot = 32767;
        constexpr std::size_t kSignedSlotCount = 65535;
        constexpr std::size_t kOriginalSlotCount = 20000;
        constexpr std::uintptr_t kNegativeGuard = address::negativeModelGuard;
        static_assert(address::negativeModelGuardResume == 0x004087F0);
        constexpr std::array<std::uint8_t, 6> kGuardOriginal = {0x56, 0x57, 0x8D, 0x7C, 0xAD, 0x00};

        static_assert(sizeof(void*) == 4, "GTA:SA 1.0 requires a 32-bit build");
        static_assert(kSignedPivot + kSignedMaxId < kSignedSlotCount);

        alignas(16) std::array<void*, kSignedSlotCount> g_signedEntries{};
        bool g_signedInstalled = false;
        std::uint32_t g_guardDestination{};

        struct ModelInfoReference
        {
            std::uintptr_t operand{};
            std::uint32_t previous{};
        };

        bool readable_span(std::uintptr_t address, std::size_t bytes)
        {
            if (!address || bytes > std::numeric_limits<std::uintptr_t>::max() - address)
                return false;

            const auto end = address + bytes;
            for (auto cursor = address; cursor < end;)
            {
                MEMORY_BASIC_INFORMATION info{};
                if (VirtualQuery(reinterpret_cast<const void*>(cursor), &info, sizeof(info)) != sizeof(info) || info.State != MEM_COMMIT ||
                    (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)))
                    return false;

                const DWORD protection = info.Protect & 0xFF;
                if (protection != PAGE_READONLY && protection != PAGE_READWRITE && protection != PAGE_WRITECOPY && protection != PAGE_EXECUTE_READ &&
                    protection != PAGE_EXECUTE_READWRITE && protection != PAGE_EXECUTE_WRITECOPY)
                    return false;

                const auto start = reinterpret_cast<std::uintptr_t>(info.BaseAddress);
                if (info.RegionSize > std::numeric_limits<std::uintptr_t>::max() - start)
                    return false;
                const auto next = start + info.RegionSize;
                if (next <= cursor)
                    return false;
                cursor = (next < end) ? next : end;
            }
            return true;
        }

        bool validate_model_info_reference(const dl_reference::ModelInfoSite& site, bool firstVariant, std::uint32_t currentTable, ModelInfoReference& reference)
        {
            const auto address = static_cast<std::uintptr_t>(firstVariant ? site.gta_address_variant_1 : site.gta_address_variant_2);
            const auto opcode = site.opcode;
            std::uint32_t displacement{};

            if (opcode == 0xBE || opcode == 0xBF)
            {
                displacement = 1;
            }
            else if (opcode == 0x8B || opcode == 0x89 || opcode == 0x39)
            {
                displacement = 3;
                std::array<std::uint8_t, 3> prefix{};
                if (!::patch::read(address, prefix.data(), prefix.size()) || prefix[0] != opcode)
                    return false;

                // modrm uses sib + disp32; sib indexes dwords without a base register
                if ((prefix[1] & 0xC7) != 0x04 || (prefix[2] & 0xC7) != 0x85 || (prefix[2] & 0x38) == 0x20)
                    return false;
            }
            else
            {
                return false;
            }

            if (!::patch::expect(address, opcode))
                return false;

            reference.operand = address + displacement;
            if (!::patch::read(reference.operand, &reference.previous, sizeof(reference.previous)))
                return false;

            return reference.previous == currentTable;
        }

        struct ModelInfoReferenceSet
        {
            std::array<ModelInfoReference, dl_reference::kModelInfoSites.size()> entries{};
            std::size_t valid{};
            std::uintptr_t firstMismatch{};
        };

        ModelInfoReferenceSet validate_model_info_references(bool firstVariant, std::uint32_t activeTable)
        {
            ModelInfoReferenceSet result{};
            for (const auto& site : dl_reference::kModelInfoSites)
            {
                auto& entry = result.entries[result.valid];
                if (!validate_model_info_reference(site, firstVariant, activeTable, entry))
                {
                    result.firstMismatch = firstVariant ? site.gta_address_variant_1 : site.gta_address_variant_2;
                    break;
                }
                ++result.valid;
            }
            return result;
        }

        bool guard_detour(std::array<std::uint8_t, 6>& result)
        {
            // ff 25 [ptr] jumps through our permanently allocated handler pointer
            g_guardDestination = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&NegativeModelGuardHook));
            const auto operand = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&g_guardDestination));
            result = {0xFF, 0x25, 0, 0, 0, 0};
            std::memcpy(result.data() + 2, &operand, sizeof(operand));
            return true;
        }

        struct SignedRelocationPlan
        {
            bool required{};
            unsigned variant{};
            std::array<ModelInfoReference, dl_reference::kModelInfoSites.size() + dl_reference::kModelInfoOffsetSites.size()> references{};
            std::array<std::uint8_t, 6> guardDetour{};
        };

        bool prepare_signed_model_info(std::uint32_t activeTable, SignedRelocationPlan& plan)
        {
            if (g_signedInstalled)
                return false;

            // don't take over a model table owned by another limit adjuster
            if (activeTable != address::vanillaModelInfoPtrs)
            {
                Log("[game::patch::modelinfo] external modelinfo table 0x%08X; signed ids unavailable", activeTable);
                return true;
            }

            if (!readable_span(activeTable, kOriginalSlotCount * sizeof(void*)) || !::patch::expect(kNegativeGuard, kGuardOriginal.data(), kGuardOriginal.size()) ||
                !guard_detour(plan.guardDetour))
            {
                Log("[game::patch::modelinfo] original table or streaming guard differs from gta 1.0 us");
                return false;
            }

            const auto first = validate_model_info_references(true, activeTable);
            const auto second = validate_model_info_references(false, activeTable);
            constexpr std::size_t expected = dl_reference::kModelInfoSites.size();
            const bool firstOK = first.valid == expected;
            const bool secondOK = second.valid == expected;
            if (firstOK == secondOK)
            {
                Log("[game::patch::modelinfo] reference validation failed/ambiguous: v1=%u (0x%08X), v2=%u (0x%08X)", static_cast<unsigned>(first.valid),
                    static_cast<unsigned>(first.firstMismatch), static_cast<unsigned>(second.valid), static_cast<unsigned>(second.firstMismatch));
                return false;
            }

            plan.variant = firstOK ? 1u : 2u;
            const auto& references = firstOK ? first.entries : second.entries;
            std::copy(references.begin(), references.end(), plan.references.begin());
            const auto& extraReferences = dl_reference::kModelInfoOffsetSites;
            for (std::size_t index = 0; index < extraReferences.size(); ++index)
            {
                auto& reference = plan.references[expected + index];
                reference.operand = extraReferences[index].operand;
                if (!::patch::read(reference.operand, &reference.previous, sizeof(reference.previous)) || reference.previous != extraReferences[index].original)
                    return false;
            }
            plan.required = true;
            return true;
        }

        bool install_signed_model_info(std::uint32_t activeTable, const SignedRelocationPlan& plan)
        {
            if (!plan.required)
                return true;

            g_signedEntries.fill(nullptr);
            if (!::patch::read(activeTable, g_signedEntries.data() + kSignedPivot, kOriginalSlotCount * sizeof(void*)))
                return false;

            const auto newBase = reinterpret_cast<std::uintptr_t>(g_signedEntries.data() + kSignedPivot);
            const auto relocated = static_cast<std::uint32_t>(newBase);

            std::size_t attempted{};
            bool success = true;
            for (const auto& entry : plan.references)
            {
                ++attempted;
                const auto target = relocated + (entry.previous - activeTable);
                if (!::patch::write(entry.operand, static_cast<std::uint32_t>(target)))
                {
                    success = false;
                    break;
                }
            }

            if (success && !::patch::write(kNegativeGuard, plan.guardDetour.data(), plan.guardDetour.size()))
                success = false;

            if (!success)
            {
                // a failed protect restore can still leave patched bytes behind
                bool restored = ::patch::write(kNegativeGuard, kGuardOriginal.data(), kGuardOriginal.size());
                while (attempted)
                {
                    const auto& entry = plan.references[--attempted];
                    restored = ::patch::write(entry.operand, entry.previous) && restored;
                }
                Log("[game::patch::modelinfo] signed relocation failed; rollback=%s", restored ? "ok" : "failed");
                return false;
            }

            g_signedInstalled = true;
            Log("[game::patch::modelinfo] signed CModelInfo installed: variant=%u, sites=%u, table=0x%08X, ids=[%d,%d]", plan.variant,
                static_cast<unsigned>(plan.references.size()), relocated, kSignedMinId, kSignedMaxId);
            return true;
        }

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

        SignedRelocationPlan signedPlan{};
        if (!prepare_signed_model_info(modelInfoPtrs, signedPlan))
            return false;

        if (!install_atomic_store())
            return false;

        if (!install_ped_store())
            return false;

        if (!patch_txd_capacity())
            return false;

        if (!patch_vehicle_struct_capacity())
            return false;

        if (!install_signed_model_info(modelInfoPtrs, signedPlan))
            return false;

        return true;
    }

    bool signed_ids_enabled()
    {
        return g_signedInstalled;
    }

    bool valid_id(std::int32_t modelId)
    {
        if (modelId >= 0)
            return modelId < 20000;
        return g_signedInstalled && modelId >= kSignedMinId;
    }

    void** active_table()
    {
        if (!g_signedInstalled)
            return reinterpret_cast<void**>(static_cast<std::uintptr_t>(active_model_info_ptrs()));
        return g_signedEntries.data() + kSignedPivot;
    }
}
