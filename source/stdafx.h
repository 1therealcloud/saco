/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * The original code is copyright (c) 2026, 1therealcloud.
 */

#pragma once

// short fade when opening the ui
#define UI_FADE

// commented out because the old spawn logo and text are legacy
// #define LEGACY_SPAWN_SCREEN

#if !defined(WIN32_LEAN_AND_MEAN)
#   define WIN32_LEAN_AND_MEAN
#endif
#if !defined(NOMINMAX)
#   define NOMINMAX
#endif

#include <Windows.h>
#include <MinHook.h>
#include <d3d9.h>
#include <tracy/Tracy.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "utils/log.inl"
#include "utils/hook.inl"
#include "utils/patch.inl"
