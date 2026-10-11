#pragma once

namespace Debug
{
    bool Initialize();

#ifdef _DEBUG
    namespace Script
    {
        // game thread only, after the player and model table are initialized
        bool Initialize(const char* file);
    }
#endif // _DEBUG
}
