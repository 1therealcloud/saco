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
        static bool installed = false;

        if (installed)
            return false;

        if (!wait_for_game_ready())
            return false;

        if (!base::install())
        {
            Log("[game::patch] base patches failed");
            return false;
        }

        if (!modelinfo::install())
        {
            Log("[game::patch] modelinfo patches failed");
            return false;
        }

        if (!streaming::install())
        {
            Log("[game::patch] streaming patches failed");
            return false;
        }

        if (!pools::install())
        {
            Log("[game::patch] pool patches failed");
            return false;
        }

        if (!game::players::Install())
            return false;

        if (!game::hooks::InstallVehicleControlHooks())
            return false;

        if (!game::hooks::InstallVehicleTransitionHooks())
            return false;

        if (!game::bullets::Install())
            return false;

        if (!vehicle::install())
        {
            Log("[game::patch] vehicle siren hook failed");
            return false;
        }

        if (!game::hooks::InstallVehicleCollisionHooks())
            return false;

        installed = true;

        Log("[game::patch] installed");
        return true;
    }
}
