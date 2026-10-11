#pragma once

enum CursorMode
{
    CURSOR_NONE,
    CURSOR_LOCKKEYS_NOCURSOR,
    CURSOR_LOCKCAMANDCONTROL,
    CURSOR_LOCKCAM,
    CURSOR_LOCKCAM_NOCURSOR
};

namespace game::cursor
{
    void SetMode(int mode);
}
