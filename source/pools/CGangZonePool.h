/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * The original code is copyright (c) 2026, 1therealcloud.
 */

#pragma once

#include <Windows.h>

#include <cstddef>
#include <cstdint>

struct GangZone
{
    struct Rect
    {
        float left;   // 0x00
        float bottom; // 0x04
        float right;  // 0x08
        float top;    // 0x0C
    } m_rect;

    std::uint32_t m_color;    // 0x10
    std::uint32_t m_altColor; // 0x14
};

class CGangZonePool
{
public:
    static constexpr std::size_t MAX_GANGZONES = 1024;

public:
    GangZone* m_pObject[MAX_GANGZONES]; // 0x0000
    BOOL m_bNotEmpty[MAX_GANGZONES];    // 0x1000

public:
    CGangZonePool();
    ~CGangZonePool();

    CGangZonePool(const CGangZonePool&) = delete;
    CGangZonePool& operator=(const CGangZonePool&) = delete;

    void Create(std::uint16_t id, float left, float top, float right, float bottom, std::uint32_t color);
    void StartFlashing(std::uint16_t id, std::uint32_t color);
    void StopFlashing(std::uint16_t id);
    void Delete(std::uint16_t id);
    void Draw();
};
