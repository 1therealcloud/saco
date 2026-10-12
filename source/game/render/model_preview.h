/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * The original code is copyright (c) 2026, 1therealcloud.
 */

#pragma once

#include <cstdint>
#include "textures.h"
#include "utils/math/CVector.h"

namespace game::render::model_preview
{
    // game thread, outside an active camera update; returns an owned 256x256 texture
    textures::Texture create(int model, CVector rotation, float zoom, std::uint32_t background, int color1 = -1, int color2 = -1);
}
