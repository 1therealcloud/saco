/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * The original code is copyright (c) 2026, 1therealcloud.
 */

#include <stdafx.h>

#include "textures.h"
#include "game/RenderWare/RenderWare.h"

namespace game::render::textures
{
    namespace
    {
        template <typename Function>
        Function gta(std::uintptr_t address)
        {
            return reinterpret_cast<Function>(address);
        }
    }

    void TextureDeleter::operator()(RwTexture* texture) const noexcept
    {
        if (texture)
            RwTextureDestroy(texture);
    }

    int find_slot(const char* name)
    {
        return name && *name ? gta<int(__cdecl*)(const char*)>(0x731850)(name) : -1;
    }

    int load_dictionary(const char* name, const char* path)
    {
        ZoneScoped;
        if (!name || !*name || !path || !*path || find_slot(name) >= 0)
            return -1;
        // the filename overload of CTxdStore::LoadTxd retries forever on a missing file
        RwStream* stream = RwStreamOpen(rwSTREAMFILENAME, rwSTREAMREAD, path);
        if (!stream)
            return -1;
        const int slot = load_dictionary(name, stream);
        RwStreamClose(stream, nullptr);
        return slot;
    }

    int load_dictionary(const char* name, RwStream* stream)
    {
        if (!stream || !name || !*name || find_slot(name) >= 0)
            return -1;
        const int slot = gta<int(__cdecl*)(const char*)>(0x731C80)(name);
        const bool loaded = slot >= 0 && gta<bool(__cdecl*)(int, RwStream*)>(0x731DD0)(slot, stream);
        if (!loaded && slot >= 0)
            remove_dictionary(slot);
        return loaded ? slot : -1;
    }

    bool remove_dictionary(int slot)
    {
        if (slot < 0 || gta<int(__cdecl*)(int)>(0x731AA0)(slot) != 0)
            return false;
        gta<void(__cdecl*)(int)>(0x731E90)(slot);
        gta<void(__cdecl*)(int)>(0x731CD0)(slot);
        return true;
    }

    void add_ref(int slot)
    {
        if (slot >= 0)
            gta<void(__cdecl*)(int)>(0x731A00)(slot);
    }

    void remove_ref(int slot)
    {
        if (slot >= 0)
            gta<void(__cdecl*)(int)>(0x731A30)(slot);
    }

    DictionaryScope::DictionaryScope(int slot)
    {
        ZoneScoped;
        if (slot < 0)
            return;
        auto* dictionary = gta<RwTexDictionary*(__cdecl*)(int)>(0x408340)(slot);
        if (!dictionary)
            return;
        m_previous = RwTexDictionaryGetCurrent();
        RwTexDictionarySetCurrent(dictionary);
        m_valid = true;
    }

    DictionaryScope::~DictionaryScope()
    {
        if (m_valid)
            RwTexDictionarySetCurrent(m_previous);
    }

    Texture read(int slot, const char* name, const char* mask)
    {
        if (!name || !*name)
            return {};
        const DictionaryScope dictionary(slot);
        if (!dictionary)
            return {};
        return Texture(RwTextureRead(name, mask));
    }
}
