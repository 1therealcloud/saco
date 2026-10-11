#pragma once

#include <d3d9.h>
#include <string>

namespace screenshot
{
    struct Result
    {
        bool requested = false;
        bool saved = false;
        int index = -1;
    };

    void Request();
    // call from the render thread; completion is reported once the PNG worker finishes
    Result ProcessPendingCapture(IDirect3DDevice9* device, const std::string& userDirectory);
}
