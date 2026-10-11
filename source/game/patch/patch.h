#pragma once

#include <cstdint>

namespace game::patch
{
    // patch.cpp
    bool install();

    // base.cpp
    namespace base
    {
        bool install();
    }

    // modelinfo.cpp
    namespace modelinfo
    {
        bool install();
    }

    // pools.cpp
    namespace pools
    {
        bool install();
    }

    namespace vehicle
    {
        namespace detail
        {
            using UsesSirenFn = bool(__thiscall*)(void*);
            using VehicleAudioFn = void(__thiscall*)(void*);
            extern VehicleAudioFn originalVehicleAudio;
            extern UsesSirenFn originalUsesSiren;
            extern bool nextVehicleHasSiren;
            extern bool detachingTrailer;
        }
        bool install();
    }

    // streaming.h
    namespace streaming
    {
        bool install();
    }

    namespace address
    {
        inline constexpr std::uintptr_t gtaEntryPoint = 0x00824570;

        inline constexpr std::uintptr_t ptrNodeSinglePool = 0x00550F46;
        inline constexpr std::uintptr_t ptrNodeDoublePool = 0x00550F82;
        inline constexpr std::uintptr_t entryInfoPool = 0x00550FBA;
        inline constexpr std::uintptr_t pedPool = 0x00550FF2;
        inline constexpr std::uintptr_t vehiclePool = 0x00551024;
        inline constexpr std::uintptr_t vehiclePoolCapacityByte = 0x0055102A;
        inline constexpr std::uintptr_t buildingPool = 0x0055105F;
        inline constexpr std::uintptr_t objectPool = 0x00551097;
        inline constexpr std::uintptr_t dummyPool = 0x005510CF;
        inline constexpr std::uintptr_t collisionModelPool = 0x00551106;
        inline constexpr std::uintptr_t taskPool = 0x0055113F;
        inline constexpr std::uintptr_t eventPool = 0x00551177;
        inline constexpr std::uintptr_t pedIntelligencePool = 0x00551283;
        inline constexpr std::uintptr_t matrixPool = 0x0054F3A1;

        inline constexpr std::uintptr_t streamingMemory = 0x005B8E6A;

        inline constexpr std::uintptr_t txdCapacity = 0x00731F60;

        inline constexpr std::uintptr_t atomicModelCount = 0x00AAE950;
        inline constexpr std::uintptr_t atomicModelBase = 0x00AAE954;
        inline constexpr std::uintptr_t pedModelBase = 0x00B478FC;
        inline constexpr std::uintptr_t pedModelBaseRef = 0x004C67AD;
        inline constexpr std::uintptr_t vehicleStructCount = 0x005B8FE4;

        inline constexpr std::uintptr_t vanillaModelInfoPtrs = 0x00A9B0C8;
        inline constexpr std::uintptr_t negativeModelGuard = 0x004087EA;
        inline constexpr std::uintptr_t negativeModelGuardResume = 0x004087F0;

        inline constexpr std::uintptr_t gameInit2 = 0x005BA1A0;
        inline constexpr std::uintptr_t gameInit2Call = 0x0053BCA6;

        inline constexpr std::uintptr_t scanListBase = 0x00B7D0B8;
        inline constexpr std::uintptr_t scanListVanillaEnd = scanListBase + 8u * 14400u;
        inline constexpr std::uintptr_t scanListVanillaEndRef = 0x00564DC7;
    }
}
