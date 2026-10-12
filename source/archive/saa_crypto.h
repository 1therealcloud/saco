/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * The original code is copyright (c) 2026, 1therealcloud.
 */

#pragma once

#include "saa.h"

#include <vector>

// wincrypt side of saa. rsa 1024 + sha1, pkcs1 without the digest oid (CRYPT_NOHASHOID),
// same as the original samp archive tool, so the signature is 128 bytes
namespace saa::win
{
    // fresh rsa pair: privateBlob is a PRIVATEKEYBLOB, publicBlob is a PUBLICKEYBLOB (148 bytes)
    bool GenerateRsaKeyPair(std::vector<std::uint8_t>& privateBlob, std::vector<std::uint8_t>& publicBlob);

    bool SignRsaSha1(const std::vector<std::uint8_t>& privateBlob, const std::uint8_t* data, std::size_t size, std::vector<std::uint8_t>& signature);

    // random bytes from the system rng (tea key)
    bool RandomBytes(std::uint8_t* destination, std::size_t count);
}
