/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * The original code is copyright (c) 2026, 1therealcloud.
 */

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
