#pragma once

#include "CRect.h"

// 0x404200
inline void CRect::Restrict(const CRect& restriction)
{
    if (restriction.left < left)
        left = restriction.left;

    if (restriction.right > right)
        right = restriction.right;

    if (restriction.bottom < bottom)
        bottom = restriction.bottom;

    if (restriction.top > top)
        top = restriction.top;
}

// 0x404260
inline void CRect::Resize(float resizeX, float resizeY)
{
    left -= resizeX;
    right += resizeX;
    bottom -= resizeY;
    top += resizeY;
}

// 0x404290
inline bool CRect::IsPointInside(const CVector2D& point) const
{
    return point.x >= left && point.x <= right && point.y >= bottom && point.y <= top;
}

// 0x4042D0
inline bool CRect::IsPointInside(const CVector2D& point, float tolerance) const
{
    return left - tolerance <= point.x && right + tolerance >= point.x && bottom - tolerance <= point.y && top + tolerance >= point.y;
}

inline bool CRect::IsRectInside(const CRect& rect) const
{
    return rect.left <= right && rect.right >= left && rect.top <= bottom && rect.bottom >= top;
}

// 0x43E020
inline void CRect::SetFromCenter(float x, float y, float size)
{
    left = x - size;
    bottom = y - size;
    right = x + size;
    top = y + size;
}

// 0x43E050
inline void CRect::GetCenter(float* x, float* y) const
{
    *x = (right + left) / 2.0f;
    *y = (bottom + top) / 2.0f;
}

// 0x5327F0
inline void CRect::StretchToPoint(float x, float y)
{
    if (x < left)
        left = x;

    if (x > right)
        right = x;

    if (y < bottom)
        bottom = y;

    if (y > top)
        top = y;
}

inline bool CRect::DoConstrainPoint(CVector2D& pt) const
{
    auto hasConstrained = false;

    const auto DoConstrain = [&](float& outPos, float constraint) {
        outPos = constraint;
        hasConstrained = true;
    };

    if (right < pt.x)
    {
        DoConstrain(pt.x, right);
    }
    else if (pt.x < left)
    {
        DoConstrain(pt.x, left);
    }

    if (pt.y > top)
    {
        DoConstrain(pt.y, top);
    }
    else if (bottom > pt.y)
    {
        DoConstrain(pt.y, bottom);
    }

    return hasConstrained;
}

// NOTSA
inline bool CRect::OverlapsWith(const CRect& o) const
{
    return (right >= o.left && left <= o.right) && (bottom <= o.top && top >= o.bottom);
}

// NOTSA
inline bool CRect::Contains(const CRect& o) const
{
    return (o.left >= left && o.right <= right) && (o.top <= top && o.bottom >= bottom);
}
