/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * The original code is copyright (c) 2026, 1therealcloud.
 */

#include <stdafx.h>

#include "authentication.h"
#include "archive/saa.h"

namespace authentication
{
    namespace
    {
        constexpr std::size_t kSha1Size = 20;
        constexpr std::size_t kLogoAuthSize = 100;

        std::uint32_t rotl32(std::uint32_t value, unsigned bits)
        {
            return (value << bits) | (value >> (32u - bits));
        }

        void sha1_transform(std::uint32_t state[5], const std::uint8_t block[64])
        {
            ZoneScoped;
            std::uint32_t words[80]{};

            for (std::size_t i = 0; i < 16; ++i)
            {
                const std::size_t p = i * 4;

                words[i] = (static_cast<std::uint32_t>(block[p + 0]) << 24) | (static_cast<std::uint32_t>(block[p + 1]) << 16) | (static_cast<std::uint32_t>(block[p + 2]) << 8) |
                           static_cast<std::uint32_t>(block[p + 3]);
            }

            for (std::size_t i = 16; i < 80; ++i)
            {
                words[i] = rotl32(words[i - 3] ^ words[i - 8] ^ words[i - 14] ^ words[i - 16], 1);
            }

            std::uint32_t a = state[0];
            std::uint32_t b = state[1];
            std::uint32_t c = state[2];
            std::uint32_t d = state[3];
            std::uint32_t e = state[4];

            for (std::size_t i = 0; i < 80; ++i)
            {
                std::uint32_t f{};
                std::uint32_t k{};

                if (i < 20)
                {
                    f = (b & c) | ((~b) & d);
                    k = 0x5A827999u;
                }
                else if (i < 40)
                {
                    f = b ^ c ^ d;
                    k = 0x6ED9EBA1u;
                }
                else if (i < 60)
                {
                    f = (b & c) | (b & d) | (c & d);
                    k = 0x8F1BBCDCu;
                }
                else
                {
                    f = b ^ c ^ d;
                    k = 0xCA62C1D6u;
                }

                const std::uint32_t temp = rotl32(a, 5) + f + e + k + words[i];

                e = d;
                d = c;
                c = rotl32(b, 30);
                b = a;
                a = temp;
            }

            state[0] += a;
            state[1] += b;
            state[2] += c;
            state[3] += d;
            state[4] += e;
        }

        std::array<std::uint8_t, kSha1Size> sha1_samp(std::string_view input)
        {
            ZoneScoped;
            std::uint32_t state[5] = {0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u};

            const auto* data = reinterpret_cast<const std::uint8_t*>(input.data());

            std::size_t remaining = input.size();

            while (remaining >= 64)
            {
                sha1_transform(state, data);

                data += 64;
                remaining -= 64;
            }

            std::uint8_t tail[128]{};

            if (remaining != 0)
                std::memcpy(tail, data, remaining);

            tail[remaining] = 0x80;

            const std::uint64_t bitLength = static_cast<std::uint64_t>(input.size()) * 8u;

            const std::size_t lengthOffset = remaining < 56 ? 56 : 120;

            for (unsigned i = 0; i < 8; ++i)
            {
                tail[lengthOffset + 7 - i] = static_cast<std::uint8_t>(bitLength >> (i * 8));
            }

            sha1_transform(state, tail);

            if (lengthOffset == 120)
                sha1_transform(state, tail + 64);

            std::array<std::uint8_t, kSha1Size> digest{};

            // samp.dll copies the five SHA-1 state DWORDs directly on x86.
            for (std::size_t i = 0; i < 5; ++i)
            {
                digest[i * 4 + 0] = static_cast<std::uint8_t>(state[i]);

                digest[i * 4 + 1] = static_cast<std::uint8_t>(state[i] >> 8);

                digest[i * 4 + 2] = static_cast<std::uint8_t>(state[i] >> 16);

                digest[i * 4 + 3] = static_cast<std::uint8_t>(state[i] >> 24);
            }

            return digest;
        }

        char hex_digit(std::uint8_t value)
        {
            return static_cast<char>(value < 10 ? ('0' + value) : ('A' + value - 10));
        }

        std::string to_hex(const std::uint8_t* data, std::size_t size)
        {
            ZoneScoped;
            std::string result;
            result.resize(size * 2);

            for (std::size_t i = 0; i < size; ++i)
            {
                result[i * 2 + 0] = hex_digit(data[i] >> 4);

                result[i * 2 + 1] = hex_digit(data[i] & 0x0F);
            }

            return result;
        }

        std::string big_integer_to_hex(const std::uint8_t* data, std::size_t size)
        {
            ZoneScoped;
            std::size_t first = 0;

            while (first < size && data[first] == 0)
                ++first;

            if (first == size)
                return "0";

            std::string result;
            result.reserve((size - first) * 2);

            const std::uint8_t firstByte = data[first++];

            if ((firstByte >> 4) != 0)
                result.push_back(hex_digit(firstByte >> 4));

            result.push_back(hex_digit(firstByte & 0x0F));

            while (first < size)
            {
                result.push_back(hex_digit(data[first] >> 4));
                result.push_back(hex_digit(data[first] & 0x0F));

                ++first;
            }

            return result;
        }

        std::uint8_t gpci_transform_byte(std::uint8_t value)
        {
            ZoneScoped;
            const std::uint8_t p0 = value & 0x03;
            const std::uint8_t p1 = (value >> 2) & 0x03;
            const std::uint8_t p2 = (value >> 4) & 0x03;
            const std::uint8_t p3 = (value >> 6) & 0x03;

            const std::uint8_t low = static_cast<std::uint8_t>((std::max(p0, p1) << 2) | std::min(p0, p1));

            const std::uint8_t high = static_cast<std::uint8_t>((std::max(p2, p3) << 2) | std::min(p2, p3));

            return static_cast<std::uint8_t>((high << 4) | low);
        }

        std::array<std::uint8_t, 24> multiply_160(const std::array<std::uint8_t, kSha1Size>& input, std::uint32_t factor)
        {
            ZoneScoped;
            std::array<std::uint8_t, 24> output{};

            std::uint64_t carry = 0;

            for (std::size_t i = input.size(); i-- > 0;)
            {
                const std::uint64_t value = static_cast<std::uint64_t>(input[i]) * factor + carry;

                output[i + 4] = static_cast<std::uint8_t>(value);

                carry = value >> 8;
            }

            for (std::size_t i = 4; i-- > 0;)
            {
                output[i] = static_cast<std::uint8_t>(carry);

                carry >>= 8;
            }

            return output;
        }

    }

    std::optional<std::string> make_auth_key(saa::Archive& archive, std::string_view challenge)
    {
        ZoneScoped;
        // the wire length includes the terminator; samp hashes the string up to the first null
        if (const auto end = challenge.find('\0'); end != std::string_view::npos)
            challenge = challenge.substr(0, end);

        auto digest = sha1_samp(challenge);

        std::uint32_t logoId = saa::kInvalidFile;

        if (!archive.FindFile("logo.png", logoId))
            return std::nullopt;

        std::uint32_t logoSize = 0;

        if (!archive.FileSize(logoId, logoSize) || logoSize < kLogoAuthSize)
        {
            return std::nullopt;
        }

        const std::uint8_t* logo = nullptr;

        if (!archive.FileData(logoId, logo) || !logo)
            return std::nullopt;

        // Original code XORs every digest byte with the first 100
        // bytes of logo.png. The per-group constants are also XORed
        // 100 times, so they cancel out completely.
        std::uint8_t logoMask = 0;

        for (std::size_t i = 0; i < kLogoAuthSize; ++i)
            logoMask ^= logo[i];

        Log("[authentication] logo mask=%02X, string length=%zu", logoMask, challenge.size());

        // FileData() caches the decrypted file; do not keep logo.png cached.
        archive.UnloadFile(logoId);

        for (std::uint8_t& byte : digest)
            byte ^= logoMask;

        // auth uses these bytes from GTA SA 1.0 US hoodlum, regardless of our executable's layout
        constexpr std::array<std::uint8_t, kSha1Size> gameCode = {0xFF, 0x25, 0x34, 0x39, 0x4D, 0x00, 0x90, 0x90, 0x90, 0x90,
                                                                  0x56, 0x57, 0x50, 0x8B, 0x44, 0x24, 0x14, 0x8D, 0x0C, 0x80};

        for (std::size_t i = 0; i < digest.size(); ++i)
            digest[i] ^= gameCode[i];

        return to_hex(digest.data(), digest.size());
    }

    std::string make_gpci(std::string_view source, std::uint32_t factor)
    {
        ZoneScoped;
        if (factor == 0)
            return "0";

        auto digest = sha1_samp(source);

        for (std::uint8_t& byte : digest)
            byte = gpci_transform_byte(byte);

        const auto product = multiply_160(digest, factor);

        return big_integer_to_hex(product.data(), product.size());
    }

}
