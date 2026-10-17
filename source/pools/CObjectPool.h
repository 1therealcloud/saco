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

#include "game/entities/CObject.h"
#include "utils/math/CVector.h"

class CObjectPool
{
public:
    using ID = std::uint16_t;

    enum
    {
        MAX_OBJECTS = 1000
    };

    static constexpr ID INVALID_ID = 0xFFFF;

    int m_nLargestId;                // 0x0000
    BOOL m_bNotEmpty[MAX_OBJECTS];   // 0x0004
    CObject* m_pObject[MAX_OBJECTS]; // 0x0FA4

    CObjectPool();  // 0x12800
    ~CObjectPool(); // 0x13160

    CObjectPool(const CObjectPool&) = delete;
    CObjectPool& operator=(const CObjectPool&) = delete;

    void UpdateLargestId();                                                                // 0x127A0
    int GetCount();                                                                        // 0x12830
    BOOL Delete(ID id);                                                                    // 0x12850
    BOOL Create(ID id, int model, CVector position, CVector rotation, float drawDistance); // 0x128D0
    CObject* Find(const void* gameObject);                                                 // 0x129D0
    int GetId(const void* gameObject);                                                     // 0x12A10
    void Process();                                                                        // 0x12A50
    void Process(float elapsedSeconds);
    void ConstructMaterials(unsigned& budget); // 0x12B10
    void ShutdownMaterials();                  // 0x12B50
    void Draw();                               // 0x12B90
    void DrawLast();                           // 0x12BD0
    CObject* Get(ID id);                       // 0x2DE0
};
