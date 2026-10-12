/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * The original code is copyright (c) 2026, 1therealcloud.
 */

#pragma once

#include <optional>

#include <cstdint>
#include <string>
#include <string_view>

#include "main.h"

namespace saa
{
    class Archive;
}

namespace authentication
{
    inline constexpr std::uint32_t kVersion037 = 4057;
    inline constexpr std::uint32_t kVersion03DL = 4062;

    inline std::uint32_t client_version()
    {
        return g_DLClient ? kVersion03DL : kVersion037;
    }

    inline std::string_view client_version_string()
    {
        return g_DLClient ? "0.3.DL-R1" : "0.3.7-R5";
    }

    inline std::uint32_t make_challenge_response(std::uint32_t challenge)
    {
        return challenge ^ client_version();
    }

    // SA-MP ID_AUTH_KEY response.
    std::optional<std::string> make_auth_key(saa::Archive& archive, std::string_view challenge);

    // SA-MP GPCI / CI used in RPC_ClientJoin.
    std::string make_gpci(std::string_view source, std::uint32_t factor = 0x3E9);

}
