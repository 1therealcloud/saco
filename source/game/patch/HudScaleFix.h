/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * The original code is copyright (c) 2026, 1therealcloud.
 */

#pragma once

namespace game::patch::hudscalefix
{
    // Installs a radar entry hook and a scoped crosshair draw wrapper.
    // The hooks are always installed; set_enabled() controls whether scaling is applied.

    void set_enabled(bool enabled);
    bool is_enabled();
}

namespace game::patch::hudscalefix
{
    namespace detail
    {
        using DrawRadarFn = void(__cdecl*)();
        extern DrawRadarFn g_drawRadar;
        struct ScaleState
        {
            float scaleX{};
            float scaleY{};
            float radarWidth{};
            float radarHeight{};
            bool applied{};
        };
    }

    class ScopedScaleFix
    {
    public:
        explicit ScopedScaleFix(bool apply);
        ~ScopedScaleFix();
        ScopedScaleFix(const ScopedScaleFix&) = delete;
        ScopedScaleFix& operator=(const ScopedScaleFix&) = delete;

    private:
        detail::ScaleState m_state{};
    };
}
