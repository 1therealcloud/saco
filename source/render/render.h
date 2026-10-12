/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * The original code is copyright (c) 2026, 1therealcloud.
 */

#pragma once

#include <d3d9.h>

namespace render
{
    extern bool g_classSelectionVisible;
    void InitializeUi(IDirect3DDevice9* device);
    void SetCursorMode(int mode, bool immediatelyHideCursor);
    void SetVehicleDebugLabels(bool enabled);
    void SetCameraTargetLabels(bool enabled);
    void ShowClassSelection(bool visible);
    void RenderWorldObjects();
    void RenderFadingObjects();
    void DrawGangZoneOverlay(bool menu);
    void RenderNativeTextDraws();
    void ShutdownUi();
    void ApplyGuiFontConfig();
    void RenderUi(IDirect3DDevice9* device);
    void LostUiDevice(IDirect3DDevice9* device);
    void ResetUiDevice(IDirect3DDevice9* device, const D3DPRESENT_PARAMETERS& parameters);
}
