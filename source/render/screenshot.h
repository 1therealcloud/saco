/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * The original code is copyright (c) 2026, 1therealcloud.
 */

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
