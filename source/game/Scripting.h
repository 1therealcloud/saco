/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * The original code is copyright (c) 2026, 1therealcloud.
 */

#pragma once

#include <cstdint>

namespace scm
{
    struct OpcodeInfo
    {
        std::uint16_t opcode;
        char params[16];
    };

    enum class EventType : std::int32_t
    {
        Paintjob = 1,
        CarComponent = 2,
        CarColor = 3,
        EnterExitModShop = 4
    };

    // call on the game thread, v takes a pointer to a 32-bit value
    void initialize();
    int ExecBuffer();
    int ProcessCommand(const OpcodeInfo* opcode, ...);

    // local outgoing scm events; param1 is a gta vehicle handle, not a network id
    void handle_09c6(EventType type, std::uint32_t param1, std::uint32_t param2, std::uint32_t param3);
    bool handle_09d3();

    bool install();
}
