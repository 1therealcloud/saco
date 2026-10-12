/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * The original code is copyright (c) 2026, 1therealcloud.
 */

#pragma once

#include <Windows.h>

#include <cstdint>

namespace game::inputhandler
{
    void filter_game_keyboard(std::uint16_t* state);
    bool game_input_blocked();

    bool install(HWND window = nullptr);
    void uninstall();
    bool installed();

    int handle_key_up(WPARAM key);
    bool handle_char(WPARAM character);
    void FinishKeyboardFilter();
    LRESULT ProcessWindowMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
}
