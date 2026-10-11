#pragma once

#include <memory>

#include "game/RenderWare/rw/rpworld.h"

namespace game::render::models
{
    // use on the game thread; keep the model's texture dictionary loaded while reading
    struct ClumpDeleter
    {
        void operator()(RpClump* clump) const noexcept;
    };
    using Model = std::unique_ptr<RpClump, ClumpDeleter>;

    // reads a standalone dff; this does not register a GTA model id or request streaming
    Model load(const char* path, int textureSlot);
    Model clone(RpClump* model);
}
