/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * The original code is copyright (c) 2026, 1therealcloud.
 */

#if defined(SACO_BUILD)
#   include <stdafx.h>
#else // SACO_BUILD
#   include "utils/log.inl"
#   include <algorithm>
#endif // SACO_BUILD

#include "bindat.h"

#include <cctype>
#include <unordered_set>

namespace saa
{
    namespace
    {
        constexpr std::uint8_t kNoChild = 0xFF;
        constexpr std::size_t kMaxBindatEntries = 254; // count is one byte and 0xFF is "no child"

        struct TreeNode
        {
            std::int32_t nameHash;
            std::uint8_t left;
            std::uint8_t right;
            std::uint8_t dataIndex;
            std::uint8_t padding;
        };
        static_assert(sizeof(TreeNode) == 8);

        // md5 (rfc 1321)
        constexpr std::uint32_t kMd5Constants[64] = {
            0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501, 0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122,
            0xfd987193, 0xa679438e, 0x49b40821, 0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8, 0x21e1cde6, 0xc33707d6,
            0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a, 0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60,
            0xbebfbc70, 0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665, 0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039,
            0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1, 0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};
        constexpr unsigned kMd5Shifts[64] = {7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20,
                                             4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};

        void Md5ProcessBlock(std::uint32_t state[4], const std::uint8_t* block)
        {
            std::uint32_t words[16];
            for (int i = 0; i < 16; ++i)
                words[i] = std::uint32_t(block[i * 4]) | (std::uint32_t(block[i * 4 + 1]) << 8) | (std::uint32_t(block[i * 4 + 2]) << 16) | (std::uint32_t(block[i * 4 + 3]) << 24);

            std::uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
            for (unsigned i = 0; i < 64; ++i)
            {
                std::uint32_t mixed;
                unsigned wordIndex;
                if (i < 16)
                {
                    mixed = (b & c) | (~b & d);
                    wordIndex = i;
                }
                else if (i < 32)
                {
                    mixed = (d & b) | (~d & c);
                    wordIndex = (5 * i + 1) & 15;
                }
                else if (i < 48)
                {
                    mixed = b ^ c ^ d;
                    wordIndex = (3 * i + 5) & 15;
                }
                else
                {
                    mixed = c ^ (b | ~d);
                    wordIndex = (7 * i) & 15;
                }

                const std::uint32_t rotated = a + mixed + kMd5Constants[i] + words[wordIndex];
                a = d;
                d = c;
                c = b;
                b = b + ((rotated << kMd5Shifts[i]) | (rotated >> (32 - kMd5Shifts[i])));
            }
            state[0] += a;
            state[1] += b;
            state[2] += c;
            state[3] += d;
        }

        // builds a balanced bst, nodes numbered in pre-order so the root ends up at index 0.
        // sorted is ascending by signed hash, left = smaller, right = larger
        std::uint8_t BuildSubtree(const std::vector<const BindatEntry*>& sorted, const std::vector<std::int32_t>& hashes, std::size_t low, std::size_t high,
                                  /* [low, high) */ std::vector<TreeNode>& nodes, std::vector<Md5Digest>& digests)
        {
            if (low >= high)
                return kNoChild;

            const std::size_t middle = low + (high - low) / 2;
            const std::uint8_t nodeIndex = static_cast<std::uint8_t>(nodes.size());
            nodes.push_back({hashes[middle], kNoChild, kNoChild, nodeIndex, 0});
            digests.push_back(sorted[middle]->digest);

            const std::uint8_t left = BuildSubtree(sorted, hashes, low, middle, nodes, digests);
            const std::uint8_t right = BuildSubtree(sorted, hashes, middle + 1, high, nodes, digests);
            nodes[nodeIndex].left = left;
            nodes[nodeIndex].right = right;
            return nodeIndex;
        }
    }

    Md5Digest Md5(const std::uint8_t* data, std::size_t size)
    {
        std::uint32_t state[4] = {0x67452301u, 0xefcdab89u, 0x98badcfeu, 0x10325476u};

        std::size_t position = 0;
        for (; position + 64 <= size; position += 64)
            Md5ProcessBlock(state, data + position);

        std::uint8_t tail[128] = {};
        const std::size_t remaining = size - position;
        if (remaining != 0)
            std::memcpy(tail, data + position, remaining);
        tail[remaining] = 0x80;
        const std::size_t tailSize = remaining < 56 ? 64 : 128;
        const std::uint64_t bitCount = static_cast<std::uint64_t>(size) * 8u;
        for (int i = 0; i < 8; ++i)
            tail[tailSize - 8 + i] = static_cast<std::uint8_t>(bitCount >> (8 * i));
        Md5ProcessBlock(state, tail);
        if (tailSize == 128)
            Md5ProcessBlock(state, tail + 64);

        Md5Digest digest;
        for (int i = 0; i < 4; ++i)
            for (int byteIndex = 0; byteIndex < 4; ++byteIndex)
                digest[i * 4 + byteIndex] = static_cast<std::uint8_t>(state[i] >> (8 * byteIndex));
        return digest;
    }

    std::uint32_t BindatNameHash(std::string_view lowercaseName)
    {
        // signed math on purpose: the original shifts a signed int right (arithmetic shift)
        auto addShifted = [](std::int32_t value, unsigned shift) {
            return static_cast<std::int32_t>(static_cast<std::uint32_t>(value) + (static_cast<std::uint32_t>(value) << shift));
        };

        std::int32_t hash = 0;
        for (const char rawCharacter : lowercaseName)
        {
            const signed char character = static_cast<signed char>(rawCharacter);
            const int folded = (character > 'Z' || character < 'A') ? character : (character | 0x20);
            hash = static_cast<std::int32_t>(static_cast<std::uint32_t>(hash) + static_cast<std::uint32_t>(folded));
            hash = addShifted(hash, 10);
            hash ^= (hash >> 6);
        }
        hash = addShifted(hash, 3);
        hash ^= (hash >> 11);
        hash = addShifted(hash, 15);
        return static_cast<std::uint32_t>(hash);
    }

    bool BuildBindat(const std::vector<BindatEntry>& entries, std::vector<std::uint8_t>& output, std::string& error)
    {
        output.clear();
        if (entries.empty() || entries.size() > kMaxBindatEntries)
        {
            error = "bindat needs 1..254 entries";
            return false;
        }

        struct Item
        {
            std::int32_t hash;
            const BindatEntry* entry;
        };
        std::vector<Item> items;
        std::unordered_set<std::int32_t> seenHashes;
        for (const auto& entry : entries)
        {
            std::string name = entry.name;
            const std::size_t separator = name.find_last_of("/\\");
            if (separator != std::string::npos)
                name = name.substr(separator + 1);
            for (auto& character : name)
                character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));

            const auto hash = static_cast<std::int32_t>(BindatNameHash(name));
            if (!seenHashes.insert(hash).second)
            {
                error = "duplicate name hash: " + name;
                return false;
            }
            items.push_back({hash, &entry});
        }
        std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
            return a.hash < b.hash;
        });

        std::vector<const BindatEntry*> sorted;
        std::vector<std::int32_t> hashes;
        for (const auto& item : items)
        {
            sorted.push_back(item.entry);
            hashes.push_back(item.hash);
        }

        std::vector<TreeNode> nodes;
        std::vector<Md5Digest> digests;
        BuildSubtree(sorted, hashes, 0, sorted.size(), nodes, digests);

        output.push_back(static_cast<std::uint8_t>(nodes.size()));
        const auto* nodeBytes = reinterpret_cast<const std::uint8_t*>(nodes.data());
        output.insert(output.end(), nodeBytes, nodeBytes + nodes.size() * sizeof(TreeNode));
        for (const auto& digest : digests)
            output.insert(output.end(), digest.begin(), digest.end());
        return true;
    }

    bool BindatFindDigest(const std::uint8_t* data, std::size_t size, std::uint32_t nameHash, Md5Digest& digest)
    {
        if (data == nullptr || size < 1)
            return false;
        const std::size_t count = data[0];
        if (size < 1 + count * (sizeof(TreeNode) + sizeof(Md5Digest)))
            return false;

        const std::uint8_t* tree = data + 1;
        const std::uint8_t* digests = tree + count * sizeof(TreeNode);
        const auto target = static_cast<std::int32_t>(nameHash);

        std::size_t index = 0;
        for (std::size_t steps = 0; index < count && steps <= count; ++steps)
        {
            TreeNode node;
            std::memcpy(&node, tree + index * sizeof(TreeNode), sizeof(TreeNode));
            if (node.nameHash == target)
            {
                if (node.dataIndex >= count)
                    return false;
                std::memcpy(digest.data(), digests + node.dataIndex * sizeof(Md5Digest), sizeof(Md5Digest));
                return true;
            }
            index = node.nameHash < target ? node.right : node.left;
        }
        return false;
    }
}
