/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * The original code is copyright (c) 2026, 1therealcloud.
 */

#include <stdafx.h>

#include "patch.h"
#include "hooks.h"
#include "game/PlayerSlots.h"
#include "sync/BulletSync.h"

#include <chrono>
#include <thread>

namespace game::patch
{
    namespace
    {
        constexpr std::uintptr_t kGameReadyMarker = 0x00747483;

        bool wait_for_game_ready()
        {
            ZoneScoped;
            // hoodlum restores this byte when gta code is ready
            for (unsigned attempt = 0; attempt <= 6000; ++attempt)
            {
                const auto marker = *reinterpret_cast<volatile const std::uint8_t*>(kGameReadyMarker);

                if (marker == 0x89)
                    return true;

                if (attempt == 6000)
                    break;

                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }

            return false;
        }
    }

    bool install()
    {
        ZoneScoped;
        static bool installed = false;

        if (installed)
            return false;

        if (!wait_for_game_ready())
            return false;

        if (!base::install())
        {
#if defined(_DEBUG)
            Log("[game::patch] base patches failed");
#endif // _DEBUG
            return false;
        }

        if (!modelinfo::install())
        {
#if defined(_DEBUG)
            Log("[game::patch] modelinfo patches failed");
#endif // _DEBUG
            return false;
        }

        if (!streaming::install())
        {
#if defined(_DEBUG)
            Log("[game::patch] streaming patches failed");
#endif // _DEBUG
            return false;
        }

        if (!pools::install())
        {
#if defined(_DEBUG)
            Log("[game::patch] pool patches failed");
#endif // _DEBUG
            return false;
        }

        if (!game::players::Install())
            return false;

        if (!game::hooks::InstallAnimationHooks())
            return false;

        if (!game::hooks::InstallVehicleControlHooks())
            return false;

        if (!game::hooks::InstallVehicleTransitionHooks())
            return false;

        if (!game::hooks::InstallBulletHooks())
            return false;

        if (!game::hooks::InstallVehicleHooks())
        {
#if defined(_DEBUG)
            Log("[game::patch] vehicle siren hook failed");
#endif // _DEBUG
            return false;
        }

        if (!game::hooks::InstallVehicleCollisionHooks())
            return false;

        installed = true;

        return true;
    }
}
