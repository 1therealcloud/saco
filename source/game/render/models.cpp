#include <stdafx.h>

#include "models.h"
#include "textures.h"
#include "game/RenderWare/RenderWare.h"

namespace game::render::models
{
    void ClumpDeleter::operator()(RpClump* clump) const noexcept
    {
        if (clump)
            RpClumpDestroy(clump);
    }

    Model load(const char* path, int textureSlot)
    {
        ZoneScoped;
        if (!path || !*path)
            return {};
        const textures::DictionaryScope dictionary(textureSlot);
        if (!dictionary)
            return {};
        RwStream* stream = RwStreamOpen(rwSTREAMFILENAME, rwSTREAMREAD, path);
        if (!stream)
            return {};
        Model model;
        // rwID_CLUMP = 0x10; skip the file header before RpClumpStreamRead
        if (RwStreamFindChunk(stream, rwID_CLUMP, nullptr, nullptr))
            model.reset(RpClumpStreamRead(stream));
        RwStreamClose(stream, nullptr);
        return model;
    }

    Model clone(RpClump* model)
    {
        return Model(model ? RpClumpClone(model) : nullptr);
    }
}
