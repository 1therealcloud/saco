#pragma once

#include <cstdint>
#include "textures.h"
#include "utils/math/CVector.h"

namespace game::render::model_preview
{
    // game thread, outside an active camera update; returns an owned 256x256 texture
    textures::Texture create(int model, CVector rotation, float zoom, std::uint32_t background, int color1 = -1, int color2 = -1);
}
