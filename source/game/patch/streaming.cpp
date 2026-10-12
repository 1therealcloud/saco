/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * The original code is copyright (c) 2026, 1therealcloud.
 */

#include <stdafx.h>

#include "patch.h"

namespace game::patch::streaming
{
    namespace
    {
        constexpr std::uint32_t kVanillaMemory = 50u * 1024u * 1024u;

        constexpr std::size_t kVanillaScanListCount = 14400;
        constexpr std::size_t kScanListCount = 20000;
        constexpr std::size_t kScanListStride = 8;

        constexpr std::size_t kVanillaScanListSize = kVanillaScanListCount * kScanListStride;

        constexpr std::size_t kScanListSize = kScanListCount * kScanListStride;

        alignas(16) std::array<std::uint8_t, kScanListSize> g_scanList{};

        // r5 does these before streaming starts
        constexpr std::uintptr_t kStreamingInitImageList = 0x004083C0;
        constexpr std::uintptr_t kStreamingInitImageListCall = 0x00590099;
        constexpr std::uintptr_t kStreamingInitFlag = 0x0053E94C;

        constexpr std::uint8_t kStreamingInitImageListVanilla = 0xB8;
        constexpr std::uint8_t kStreamingInitImageListDisabled = 0xC3;
        constexpr std::uint8_t kStreamingInitFlagVanilla = 0x0E;
        constexpr std::uint8_t kStreamingInitFlagSamp = 0x02;

        constexpr std::array<std::uint8_t, 5> kStreamingInitImageListCallVanilla = {0xE8, 0xC2, 0x7A, 0x19, 0x00};

        constexpr std::array<std::uint8_t, 5> kNops5 = {0x90, 0x90, 0x90, 0x90, 0x90};

        constexpr std::array<std::uintptr_t, 14> kScanBaseDirect = {
            0x005DC7AA, 0x0041A85D, 0x0041A864, 0x00408259, 0x00711B32, 0x00699CF8, 0x004092EC, 0x00409151, 0x00408702, 0x00564220, 0x00564172, 0x00563845, 0x0084E9C2, 0x0085652D,
        };

        constexpr std::array<std::uintptr_t, 56> kScanBaseOffset3 = {
            0x0040D68C, 0x005664D7, 0x00566586, 0x00408706, 0x0056B3B1, 0x0056AD91, 0x0056A85F, 0x005675FA, 0x0056CD84, 0x0056CC79, 0x0056CB51, 0x0056CA4A, 0x0056C664, 0x0056C569,
            0x0056C445, 0x0056C341, 0x0056BD46, 0x0056BC53, 0x0056BE56, 0x0056A940, 0x00567735, 0x00546738, 0x0054BB23, 0x006E31AA, 0x0040DC29, 0x00534A09, 0x00534D6B, 0x00564B59,
            0x00564DA9, 0x0067FF5D, 0x00568CB9, 0x00568EFB, 0x00569F57, 0x00569537, 0x00569127, 0x0056B4B5, 0x0056B594, 0x0056B2C3, 0x0056AF74, 0x0056AE95, 0x0056BF4F, 0x0056ACA3,
            0x0056A766, 0x0056A685, 0x0070B9BA, 0x0056479D, 0x0070ACB2, 0x006063C7, 0x00699CFE, 0x0041A861, 0x0040E061, 0x0040DF5E, 0x0040DDCE, 0x0040DB0E, 0x0040D98C, 0x01566855,
        };

        constexpr std::array<std::uintptr_t, 11> kScanBasePlus4Offset3 = {
            0x004091C5, 0x00409367, 0x0040D9C5, 0x0040DB47, 0x0040DC61, 0x0040DE07, 0x0040DF97, 0x0040E09A, 0x00534A98, 0x00534DFA, 0x0071CDB0,
        };

        constexpr std::array<std::uintptr_t, 4> kScanEndDirect = {
            0x005634A6,
            0x005638DF,
            0x0056420F,
            0x00564283,
        };

        constexpr std::uintptr_t kScanBasePlus4Direct = 0x0040936A;

        std::uint32_t memory_limit()
        {
            ZoneScoped;
            MEMORYSTATUSEX status{};
            status.dwLength = sizeof(status);

            if (!GlobalMemoryStatusEx(&status))
                return 0;

            const auto megabytes = static_cast<std::uint32_t>(status.ullTotalPhys >> 20);

            if (megabytes > 4000)
                return 1024u * 1024u * 1024u;

            if (megabytes > 2000)
                return 512u * 1024u * 1024u;

            if (megabytes > 1000)
                return 256u * 1024u * 1024u;

            if (megabytes > 500)
                return 128u * 1024u * 1024u;

            return 96u * 1024u * 1024u;
        }

        bool apply_r5_bootstrap_patches()
        {
            ZoneScoped;
            std::uint8_t byte{};

            if (!::patch::read(kStreamingInitImageList, &byte, sizeof(byte)))
                return false;

            if (byte == kStreamingInitImageListVanilla)
            {
                if (!::patch::write(kStreamingInitImageList, kStreamingInitImageListDisabled))
                    return false;
            }
            else if (byte != kStreamingInitImageListDisabled)
                return false;

            if (::patch::expect(kStreamingInitImageListCall, kStreamingInitImageListCallVanilla.data(), kStreamingInitImageListCallVanilla.size()))
            {
                if (!::patch::write(kStreamingInitImageListCall, kNops5.data(), kNops5.size()))
                    return false;
            }
            else if (!::patch::expect(kStreamingInitImageListCall, kNops5.data(), kNops5.size()))
                return false;

            if (!::patch::read(kStreamingInitFlag, &byte, sizeof(byte)))
                return false;

            if (byte == kStreamingInitFlagVanilla)
            {
                if (!::patch::write(kStreamingInitFlag, kStreamingInitFlagSamp))
                    return false;
            }
            else if (byte != kStreamingInitFlagSamp)
                return false;

            return true;
        }

        bool validate_scan_list()
        {
            ZoneScoped;
            const auto oldBase = static_cast<std::uint32_t>(address::scanListBase);

            const auto oldBasePlus4 = oldBase + 4;

            const auto oldEnd = static_cast<std::uint32_t>(address::scanListVanillaEnd);

            for (const auto ref : kScanBaseDirect)
            {
                if (!::patch::expect(ref, oldBase))
                    return false;
            }

            for (const auto ref : kScanBaseOffset3)
            {
                if (!::patch::expect(ref + 3, oldBase))
                    return false;
            }

            for (const auto ref : kScanBasePlus4Offset3)
            {
                if (!::patch::expect(ref + 3, oldBasePlus4))
                    return false;
            }

            for (const auto ref : kScanEndDirect)
            {
                if (!::patch::expect(ref, oldEnd))
                    return false;
            }

            if (!::patch::expect(kScanBasePlus4Direct, oldBasePlus4))
                return false;

            return ::patch::expect(address::scanListVanillaEndRef, oldEnd);
        }

        bool relocate_scan_list()
        {
            ZoneScoped;
            const auto newBase = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(g_scanList.data()));

            const auto newBasePlus4 = newBase + 4;

            const auto newVanillaEnd = newBase + static_cast<std::uint32_t>(kVanillaScanListSize);

            const auto newEnd = newBase + static_cast<std::uint32_t>(g_scanList.size());

            g_scanList.fill(0);

            for (const auto ref : kScanBaseDirect)
            {
                if (!::patch::write(ref, newBase))
                    return false;
            }

            for (const auto ref : kScanBaseOffset3)
            {
                if (!::patch::write(ref + 3, newBase))
                    return false;
            }

            for (const auto ref : kScanBasePlus4Offset3)
            {
                if (!::patch::write(ref + 3, newBasePlus4))
                    return false;
            }

            for (const auto ref : kScanEndDirect)
            {
                if (!::patch::write(ref, newEnd))
                    return false;
            }

            if (!::patch::write(address::scanListVanillaEndRef, newVanillaEnd))
                return false;

            if (!::patch::write(kScanBasePlus4Direct, newBasePlus4))
                return false;

            return ::patch::fill(address::scanListBase, 0, kVanillaScanListSize);
        }
    }

    bool install()
    {
        ZoneScoped;
        const std::uint32_t limit = memory_limit();

        if (!limit)
            return false;

        if (!::patch::expect(address::streamingMemory, kVanillaMemory))
            return false;

        if (!validate_scan_list())
            return false;

        if (!::patch::write(address::streamingMemory, limit))
            return false;

        if (!apply_r5_bootstrap_patches())
            return false;

        return relocate_scan_list();
    }
}
