#pragma once

#include <Windows.h>

#include <cstdint>

namespace game::inputhandler
{
    void filter_game_keyboard(std::uint16_t* state);
    bool game_input_blocked();

    bool install(HWND window = nullptr);
    void rehook();
    void uninstall();
    bool installed();

    int handle_key_up(WPARAM key);
    bool handle_char(WPARAM character);
    LRESULT CALLBACK wnd_proc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
}
