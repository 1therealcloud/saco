/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * The original code is copyright (c) 2026, 1therealcloud.
 */

#pragma once

#include <d3d9.h>

namespace d3d9hook
{
    // call on initialization once the game's device exists
    bool install();
    bool matrices(D3DMATRIX& view, D3DMATRIX& projection);
}
