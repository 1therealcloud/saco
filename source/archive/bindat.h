/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * The original code is copyright (c) 2026, 1therealcloud.
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// bindat.bin layout (same as samp 0.2x/0.3.7):
//   u8            count
//   tree[count]   { i32 nameHash; u8 left; u8 right; u8 dataIndex; u8 padding; }   root is tree[0]
//   md5[count]    16 bytes each, indexed by dataIndex
// left/right >= count means "no child". tree is a plain bst on the signed nameHash.
namespace saa
{
    using Md5Digest = std::array<std::uint8_t, 16>;

    Md5Digest Md5(const std::uint8_t* data, std::size_t size);

    // the "one at a time" hash samp uses for ide/ipl names. expects a lowercased file name without a path
    std::uint32_t BindatNameHash(std::string_view lowercaseName);

    struct BindatEntry
    {
        std::string name; // file name only, lowercased inside
        Md5Digest digest; // md5 of the CLEAN game file
    };

    bool BuildBindat(const std::vector<BindatEntry>& entries, std::vector<std::uint8_t>& output, std::string& error);

    // reader side. false if the hash isn't listed or the blob is malformed
    bool BindatFindDigest(const std::uint8_t* data, std::size_t size, std::uint32_t nameHash, Md5Digest& digest);
}
