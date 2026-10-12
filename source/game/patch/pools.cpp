#include <stdafx.h>

#include "patch.h"

namespace game::patch::pools
{
    namespace
    {
        constexpr std::uint32_t kPtrNodeSingleVanilla = 70000;
        constexpr std::uint32_t kPtrNodeSingleSamp = 100000;

        constexpr std::uint32_t kPtrNodeDoubleVanilla = 3200;
        constexpr std::uint32_t kPtrNodeDoubleSamp = 8000;

        constexpr std::uint32_t kEntryInfoVanilla = 500;
        constexpr std::uint32_t kEntryInfoSamp = 5000;

        constexpr std::uint8_t kPedVanilla = 140;
        constexpr std::uint8_t kPedSamp = 240;

        constexpr std::uint32_t kBuildingVanilla = 13000;
        constexpr std::uint32_t kBuildingSamp = 20000;

        constexpr std::uint32_t kObjectVanilla = 350;
        constexpr std::uint32_t kObjectSamp = 3000;

        constexpr std::uint32_t kDummyVanilla = 2500;
        constexpr std::uint32_t kDummySamp = 4000;

        constexpr std::uint8_t kPedIntelligenceVanilla = 140;
        constexpr std::uint8_t kPedIntelligenceSamp = 240;

        constexpr std::uint32_t kTaskVanilla = 500;
        constexpr std::uint8_t kTaskHighByteSamp = 0x05;

        constexpr std::uint32_t kEventVanilla = 200;
        constexpr std::uint8_t kEventHighByteSamp = 0x01;

        constexpr std::uint32_t kMatrixVanilla = 900;
        constexpr std::uint32_t kMatrixSamp = 6000;

        constexpr std::uint32_t kCollisionVanilla = 10150;

        // col slots are separate from the col model pool
        constexpr std::uint32_t kColSlotVanilla = 255;
        constexpr std::uint32_t kColSlotSamp = 1024;
        constexpr std::uint32_t kColSlotStride = 0x2C;
        constexpr std::uint32_t kColSlotSpanVanilla = kColSlotVanilla * kColSlotStride;
        constexpr std::uint32_t kColSlotSpanSamp = kColSlotSamp * kColSlotStride;

        constexpr std::uintptr_t kColSlotCapacityRef = 0x00411458;
        constexpr std::uintptr_t kColSlotIndexLimitRef = 0x00410D92;

        constexpr std::array<std::uintptr_t, 7> kColSlotSpanRefs = {
            0x00410AA6, 0x00410CC6, 0x00410DBD, 0x00410DE7, 0x00410EAF, 0x00410F6A, 0x00411507,
        };
        constexpr std::array<std::uint8_t, 5> kCollisionSamp = {0x68, 0xFF, 0x7E, 0x00, 0x00};

        constexpr std::array<std::uint8_t, 7> kVehicleSamp = {0x6A, 0x00, 0x68, 0xC6, 0x02, 0x00, 0x00};

        bool patch_vehicle_pool()
        {
            ZoneScoped;
            constexpr std::uint8_t opcode = 0x68;
            constexpr std::uint8_t vanillaCapacity = 110;

            if (!::patch::expect(address::vehiclePool, opcode))
                return false;

            if (!::patch::expect(address::vehiclePoolCapacityByte, vanillaCapacity))
                return false;

            return ::patch::write(address::vehiclePool, kVehicleSamp.data(), kVehicleSamp.size());
        }

        bool patch_col_store()
        {
            ZoneScoped;
            if (!::patch::expect(kColSlotCapacityRef, kColSlotVanilla))
                return false;

            if (!::patch::expect(kColSlotIndexLimitRef, kColSlotVanilla))
                return false;

            for (const auto ref : kColSlotSpanRefs)
            {
                if (!::patch::expect(ref, kColSlotSpanVanilla))
                    return false;
            }

            if (!::patch::write(kColSlotCapacityRef, kColSlotSamp))
                return false;

            if (!::patch::write(kColSlotIndexLimitRef, kColSlotSamp))
                return false;

            for (const auto ref : kColSlotSpanRefs)
            {
                if (!::patch::write(ref, kColSlotSpanSamp))
                    return false;
            }

            return true;
        }

        bool patch_collision_pool()
        {
            ZoneScoped;
            constexpr std::uint8_t push = 0x68;

            if (!::patch::expect(address::collisionModelPool, push))
                return false;

            if (!::patch::expect(address::collisionModelPool + 1, kCollisionVanilla))
                return false;

            return ::patch::write(address::collisionModelPool, kCollisionSamp.data(), kCollisionSamp.size());
        }

        bool patch_task_pool()
        {
            if (!::patch::expect(address::taskPool, kTaskVanilla))
                return false;

            return ::patch::write(address::taskPool + 1, kTaskHighByteSamp);
        }

        bool patch_event_pool()
        {
            if (!::patch::expect(address::eventPool, kEventVanilla))
                return false;

            return ::patch::write(address::eventPool + 1, kEventHighByteSamp);
        }
    }

    bool install()
    {
        ZoneScoped;
        if (!::patch::replace(address::ptrNodeSinglePool, kPtrNodeSingleVanilla, kPtrNodeSingleSamp))
            return false;

        if (!::patch::replace(address::ptrNodeDoublePool, kPtrNodeDoubleVanilla, kPtrNodeDoubleSamp))
            return false;

        if (!::patch::replace(address::entryInfoPool, kEntryInfoVanilla, kEntryInfoSamp))
            return false;

        if (!::patch::replace(address::pedPool, kPedVanilla, kPedSamp))
            return false;

        if (!patch_vehicle_pool())
            return false;

        if (!::patch::replace(address::buildingPool, kBuildingVanilla, kBuildingSamp))
            return false;

        if (!::patch::replace(address::objectPool, kObjectVanilla, kObjectSamp))
            return false;

        if (!::patch::replace(address::dummyPool, kDummyVanilla, kDummySamp))
            return false;

        if (!patch_col_store())
            return false;

        if (!patch_collision_pool())
            return false;

        if (!patch_task_pool())
            return false;

        if (!patch_event_pool())
            return false;

        if (!::patch::replace(address::pedIntelligencePool, kPedIntelligenceVanilla, kPedIntelligenceSamp))
            return false;

        if (!::patch::replace(address::matrixPool, kMatrixVanilla, kMatrixSamp))
            return false;

        return true;
    }
}
