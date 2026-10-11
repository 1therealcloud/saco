/*
    Plugin-SDK file
    Authors: GTA Community. See more here
    https://github.com/DK22Pac/plugin-sdk
    Do not delete this comment block. Respect others' work!
*/
#pragma once

#include "game/RenderWare/RenderWare.h"
#include "CVector.h"

class CSimpleTransform
{
public:
    CSimpleTransform() : m_vPosn(), m_fHeading(0.0F)
    {}

public:
    CVector m_vPosn;
    float m_fHeading;

    void UpdateRwMatrix(RwMatrix* out);
    void Invert(const CSimpleTransform& base);
    void UpdateMatrix(class CMatrix* out);
};

static_assert(sizeof(CSimpleTransform) == 0x10);
