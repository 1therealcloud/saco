#pragma once

#include <d3d9.h>

namespace d3d9hook
{
    // call on initialization once the game's device exists
    bool install();
    bool matrices(D3DMATRIX& view, D3DMATRIX& projection);
}
