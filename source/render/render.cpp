/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * The original code is copyright (c) 2026, 1therealcloud.
 */

#include <stdafx.h>
#include "pools/CPlayerPool.h"

#include "game/CGame.h"

#include "render.h"

#include "game/Cursor.h"
#include "net/CNetGame.h"
#include "net/authentication.h"
#include "pools/CVehiclePool.h"
#include "pools/CObjectPool.h"
#include "players/CLocalPlayer.h"
#include "players/CRemotePlayer.h"
#include "pools/CLabelPool.h"
#include "pools/CGangZonePool.h"
#include "gui/CLabel.h"
#include "gui/CPlayerTags.h"
#include "gui/CChatBubble.h"
#include "screenshot.h"
#include "main.h"
#include "config/CConfig.h"
#include "config/CSettings.h"
#include "game/render/CObjectMaterialText.h"
#include "game/render/CLicensePlate.h"
#include "game/entities/CObject.h"
#include "game/entities/CPed.h"
#include "gui/CChat.h"
#include "audio/CAudioStream.h"
#include "gui/CInput.h"
#include "gui/CGui.h"
#include "gui/ime.h"
#include "gui/CDialog.h"
#include "gui/CHelpDialog.h"
#include "gui/CScoreboard.h"
#include "gui/CDeathWindow.h"
#include "gui/CDownloadWindow.h"
#if defined(LEGACY_SPAWN_SCREEN)
#   include "gui/CSpawnScreen.h"
#endif // LEGACY_SPAWN_SCREEN
#include "gui/CObjectEdit.h"
#include "gui/CObjectSelection.h"
#include "pools/CMenuPool.h"
#include "pools/CTextDrawPool.h"
#include "net/CNetStats.h"
#include "net/CSrvNetStats.h"
#include "gui/CTextDrawSelection.h"
#include "CFonts.h"
#include "CRender.h"

std::unique_ptr<CFonts> g_fonts;
std::unique_ptr<CRender> g_render;
std::unique_ptr<CChat> g_chat;
std::unique_ptr<CInput> g_input;
std::unique_ptr<CDialog> g_dialog;
std::unique_ptr<CHelpDialog> g_helpDialog;
std::unique_ptr<CScoreboard> g_scoreboard;
std::unique_ptr<CDeathWindow> g_deathWindow;
std::unique_ptr<CDownloadWindow> g_downloadWindow;
std::unique_ptr<CLicensePlate> g_licensePlate;
std::unique_ptr<CObjectMaterialText> g_objectMaterialText;
std::unique_ptr<CNetStats> g_netStats;
std::unique_ptr<CSrvNetStats> g_srvNetStats;
std::unique_ptr<CLabel> g_label;
std::unique_ptr<CPlayerTags> g_playerTags;
std::unique_ptr<CChatBubble> g_chatBubble;
#if defined(LEGACY_SPAWN_SCREEN)
std::unique_ptr<CSpawnScreen> g_spawnScreen;
#endif // LEGACY_SPAWN_SCREEN
std::unique_ptr<CObjectEdit> g_objectEdit;
std::unique_ptr<CObjectSelection> g_objectSelection;
std::unique_ptr<CTextDrawSelection> g_textDrawSelection;

namespace render
{
    void DrawPlayerTags();
    bool ResolvePlayerLabel(std::uint16_t id, CVector& position, float& localDistance, float& cameraDistance);
    void ApplyGuiFontConfig();
    bool g_classSelectionVisible = false;
    bool g_vehicleDebugLabels = false;
    static bool g_shuttingDown = false;
    void ShutdownUi()
    {
        ZoneScoped;
        if (g_shuttingDown)
            return;
        g_shuttingDown = true;
        g_srvNetStats.reset();
        g_netStats.reset();
        g_textDrawSelection.reset();
        g_objectEdit.reset();
        g_objectSelection.reset();
        g_scoreboard.reset();
        g_dialog.reset();
        g_helpDialog.reset();
        g_downloadWindow.reset();
        g_input.reset();
        g_playerTags.reset();
        g_chatBubble.reset();
        g_label.reset();
        g_deathWindow.reset();
#if defined(LEGACY_SPAWN_SCREEN)
        g_spawnScreen.reset();
#endif // LEGACY_SPAWN_SCREEN
        CGui::Shutdown();
        g_chat.reset();
        g_objectMaterialText.reset();
        g_licensePlate.reset();
        g_render.reset();
        g_fonts.reset();
    }

    void RenderWorldObjects()
    {
        ZoneScoped;
        auto* pools = g_netGame ? g_netGame->m_pPools : nullptr;
        auto* objectPool = pools ? pools->m_pObject : nullptr;

        if (auto* game = g_game.get())
            if (auto* ped = game->GetPlayerPed())
                ped->ProcessAccessories();
        if (auto* players = pools ? pools->m_pPlayer : nullptr)
            for (int id = 0; id <= players->m_nLargestId && id < CPlayerPool::MAX_PLAYERS; ++id)
                if (auto* remote = players->GetPlayer(static_cast<CPlayerPool::ID>(id)))
                    if (auto* ped = remote->m_pPed)
                        ped->ProcessAccessories();
        if (objectPool)
            objectPool->Draw();
    }

    void RenderFadingObjects()
    {
        ZoneScoped;
        auto* pools = g_netGame ? g_netGame->m_pPools : nullptr;
        auto* objectPool = pools ? pools->m_pObject : nullptr;

        if (objectPool)
            objectPool->DrawLast();
    }

    void DrawGangZoneOverlay(bool)
    {
        ZoneScoped;
        auto* pools = g_netGame ? g_netGame->m_pPools : nullptr;
        auto* gangZonePool = pools ? pools->m_pGangZone : nullptr;

        if (gangZonePool)
            gangZonePool->Draw();
    }

    static bool g_cameraTargetLabels = false;

    void SetCameraTargetLabels(bool enabled)
    {
        g_cameraTargetLabels = enabled;
    }

    static void DrawVehicleDebugLabels()
    {
        ZoneScoped;
        auto* pools = g_netGame ? g_netGame->m_pPools : nullptr;
        auto* vehiclePool = pools ? pools->m_pVehicle : nullptr;

        if (!g_vehicleDebugLabels || !vehiclePool || !g_label)
            return;
        auto* game = g_game.get();
        if (!game || !game->GetPlayerPed() || !game->GetPlayerPed()->IsValid())
            return;
        g_label->Begin();
        for (CVehiclePool::ID id = 0; id < CVehiclePool::MAX_VEHICLES; ++id)
        {
            auto* vehicle = vehiclePool->Get(id);
            if (!vehicle || !vehicle->IsValid())
                continue;
            const float distance = vehicle->GetDistanceToLocalPlayer();
            if (!(distance < 20.0f))
                continue;
            CMatrix matrix = CMatrix::Unity();
            vehicle->GetMatrix(&matrix);
            CVector position = matrix.GetPosition();
            const auto& spawned = vehiclePool->m_spawnedAt[id];
            const int model = vehicle->GetModelIndex();
            const unsigned preloaded = model >= 400 && model <= 611 ? static_cast<unsigned>(game->m_aKeepLoadedVehicleModels[model - 400]) : 0;
            char text[512];
            std::snprintf(text, sizeof(text),
                          "[id: %d, type: %d subtype: %d Health: %.1f preloaded: %u]\n"
                          "Distance: %.2fm\nPassengerSeats: %u\ncPos: %.3f,%.3f,%.3f\nsPos: %.3f,%.3f,%.3f\n",
                          static_cast<int>(id), model, vehicle->GetSubtype(), vehicle->GetHealth(), preloaded, distance,
                          static_cast<unsigned>(static_cast<unsigned char>(vehicle->GetMaxPassengers())), position.x, position.y, position.z, spawned.x, spawned.y, spawned.z);
            g_label->Draw(&position, text, 0xFF358BD4, TRUE, false);
        }
        g_label->End();
    }

#if defined(_DEBUG)
    static unsigned int g_fps = 0;
    static double g_frameTimeMs = 0.0;

    static void UpdateFps()
    {
        ZoneScoped;
        static ULONGLONG last = GetTickCount64();
        static unsigned int frames = 0;
        const ULONGLONG now = GetTickCount64();
        ++frames;
        const ULONGLONG elapsed = now - last;
        if (elapsed >= 1000)
        {
            g_fps = static_cast<unsigned int>(frames * 1000.0 / elapsed + 0.5);
            g_frameTimeMs = static_cast<double>(elapsed) / frames;
            frames = 0;
            last = now;
        }
    }
#endif // _DEBUG

    void ApplyGuiFontConfig()
    {
        ZoneScoped;
        int size = 0;
        int weight = 0;
        std::string face = "Arial";
        g_config.GetParam(CConfig::PARAM_FONT_SIZE, size);
        g_config.GetParam(CConfig::PARAM_FONT_WEIGHT, weight);
        g_config.GetParam(CConfig::PARAM_FONT_FACE, face);
        bool imeEnabled = false;
        g_config.GetParam(CConfig::PARAM_IME, imeEnabled);
        cgui::ime::SetEnabled(imeEnabled);
        CGui::ApplyR5FontConfig(size, weight, face.c_str());
        if (g_input)
            g_input->UpdateLayout();
        if (g_dialog)
            g_dialog->UpdateRects();
    }

    void SetCursorMode(int mode, bool immediate)
    {
        ZoneScoped;
        if (mode == 0 && g_classSelectionVisible)
            mode = 2;
        if (mode == 0 && g_input && g_input->IsEnabled())
            mode = 2;
        if (mode == 0 && g_scoreboard && g_scoreboard->m_bIsEnabled)
            mode = 3;
        if (mode == 0 && ((g_objectEdit && g_objectEdit->m_bEnabled) || (g_objectSelection && g_objectSelection->m_bIsActive)))
            mode = 2;
        if (mode == 0 && g_dialog && g_dialog->IsActive())
            mode = 2;
        if (mode == 0 && g_downloadWindow && g_downloadWindow->IsActive())
            mode = 2;
        if (mode == 0 && g_textDrawSelection && g_textDrawSelection->m_bIsActive)
            mode = 2;
        if (auto* game = g_game.get())
            game->SetCursorMode(mode, immediate ? TRUE : FALSE);
        else
            game::cursor::SetMode(mode);
        if (auto* device = CGui::GetDevice())
            device->ShowCursor(FALSE);
    }

    void ShowClassSelection(bool visible)
    {
        ZoneScoped;
        auto* pools = g_netGame ? g_netGame->m_pPools : nullptr;
        auto* localPlayer = pools && pools->m_pPlayer ? pools->m_pPlayer->GetLocalPlayer() : nullptr;

        g_classSelectionVisible = visible;
        if (localPlayer)
            localPlayer->m_classSelection.m_bIsActive = visible;
#if defined(LEGACY_SPAWN_SCREEN)
        if (g_spawnScreen)
            g_spawnScreen->m_bEnabled = visible;
#endif // LEGACY_SPAWN_SCREEN
        if (CGui::IsInitialized())
        {
            auto* dialog = CGui::GetDialog(CGui::DialogId::ClassSelection);
            dialog->SetVisible(visible);
            if (!visible)
                dialog->RequestFocus(nullptr);
            SetCursorMode(visible ? 2 : 0, false);
        }
    }

    static void UpdateClassSelection(IDirect3DDevice9* device)
    {
        ZoneScoped;
        auto* dialog = CGui::GetDialog(CGui::DialogId::ClassSelection);
        D3DVIEWPORT9 viewport{};
        if (SUCCEEDED(device->GetViewport(&viewport)))
            dialog->SetLocation(static_cast<int>(viewport.X) + (static_cast<int>(viewport.Width) - 310) / 2, static_cast<int>(viewport.Y + viewport.Height) - 80);
        dialog->SetVisible(g_classSelectionVisible);
    }

    void InitializeUi(IDirect3DDevice9* device)
    {
        ZoneScoped;
        if (g_fonts)
            return;
        g_fonts = std::make_unique<CFonts>(device, &g_config);
        if (!g_fonts->GetFont())
        {
#if defined(_DEBUG)
            Log("failed to create fonts!");
#endif // _DEBUG
            ExitProcess(1);
        }
        g_render = std::make_unique<CRender>(device);
        ApplyGuiFontConfig();
        if (!CGui::Initialize(device))
        {
#if defined(_DEBUG)
            Log("failed to initialize GUI!");
#endif // _DEBUG
            MessageBoxA(nullptr,
                        "Failed to initialize GUI.\r\nCheck that sampgui.png and mouse.png are present "
                        "in the game directory.",
                        "SA:MP - GUI Error", MB_OK | MB_ICONERROR);
            ExitProcess(1);
        }
        const auto& userDirectory = g_sampUserDirectory;
        if (userDirectory.empty())
        {
#if defined(_DEBUG)
            Log("failed to resolve chat log directory!");
#endif // _DEBUG
            ExitProcess(1);
        }
        const std::string chatLogPath = userDirectory + "chatlog.txt";
        g_chat = std::make_unique<CChat>(device, g_fonts.get(), chatLogPath.c_str(), &g_config);
        g_input = std::make_unique<CInput>(device);
        g_netStats = std::make_unique<CNetStats>(device);
        g_srvNetStats = std::make_unique<CSrvNetStats>(device);
        auto* gameUi = CGui::GetDialog(CGui::DialogId::GameUI);
        g_chat->ResetDialogControls(gameUi);
        g_input->ResetDialogControls(gameUi);
        g_dialog = std::make_unique<CDialog>(device);
        g_helpDialog = std::make_unique<CHelpDialog>(device);
        g_scoreboard = std::make_unique<CScoreboard>(device);
        g_deathWindow = std::make_unique<CDeathWindow>(device);
        g_downloadWindow = std::make_unique<CDownloadWindow>(device);
#if defined(LEGACY_SPAWN_SCREEN)
        g_spawnScreen = std::make_unique<CSpawnScreen>(device);
#endif // LEGACY_SPAWN_SCREEN
        auto* classSelection = CGui::GetDialog(CGui::DialogId::ClassSelection);
        classSelection->SetCallback([](unsigned int event, int id, CGui::Control*, void*) {
            if (event != CGui::EVENT_BUTTON_CLICKED)
                return;
            auto* pools = g_netGame ? g_netGame->m_pPools : nullptr;
            auto* localPlayer = pools && pools->m_pPlayer ? pools->m_pPlayer->GetLocalPlayer() : nullptr;
            if (id == 6 && localPlayer)
                localPlayer->PrevClass();
            else if (id == 5 && localPlayer)
                localPlayer->NextClass();
            else if (id == 4)
            {
                ZoneScoped;
#if defined(_DEBUG)
                if (g_settings.m_bDebugMode)
                {
                    ShowClassSelection(false);
                    return;
                }
#endif
                if (localPlayer)
                    localPlayer->RequestSpawn();
            }
        });
        g_objectEdit = std::make_unique<CObjectEdit>(device);
        g_objectSelection = std::make_unique<CObjectSelection>();
        g_textDrawSelection = std::make_unique<CTextDrawSelection>();
        g_objectMaterialText = std::make_unique<CObjectMaterialText>(device);
        g_licensePlate = std::make_unique<CLicensePlate>(device);
        g_label = std::make_unique<CLabel>(device);
        g_playerTags = std::make_unique<CPlayerTags>(device);
        g_chatBubble = std::make_unique<CChatBubble>();
        if (!g_settings.m_bDebugMode)
        {
            const auto version = authentication::client_version_string();
            const std::string message = "{FFFFFF}SA-MP {B9C9BF}" + std::string(version) + " {FFFFFF}Started.";
            g_chat->AddMessage(0xFFFFFFFFu, message.c_str());
        }
    }

    static void ProcessScreenshot(IDirect3DDevice9* device)
    {
        ZoneScoped;
        const auto capture = screenshot::ProcessPendingCapture(device, g_sampUserDirectory);
        if (capture.requested)
        {
            if (capture.saved)
                g_chat->PrintInfo("Screenshot Taken - sa-mp-%03i.png", capture.index);
            else
                g_chat->PrintDebug("Unable to save screenshot.");
        }
    }

    static void RenderCursor(IDirect3DDevice9* device)
    {
        ZoneScoped;
        if (g_game && g_game->IsMenuVisible())
            return;
        const bool active = (g_input && g_input->IsEnabled()) || (g_dialog && g_dialog->IsActive()) || (g_scoreboard && g_scoreboard->m_bIsEnabled) ||
                            (g_objectEdit && g_objectEdit->m_bEnabled) || (g_objectSelection && g_objectSelection->m_bIsActive) ||
                            (g_textDrawSelection && g_textDrawSelection->m_bIsActive) || (g_downloadWindow && g_downloadWindow->IsActive()) || g_classSelectionVisible;
        if (!active)
            return;
        D3DDEVICE_CREATION_PARAMETERS parameters{};
        if (FAILED(device->GetCreationParameters(&parameters)) || !parameters.hFocusWindow || GetForegroundWindow() != GetAncestor(parameters.hFocusWindow, GA_ROOT))
            return;
        POINT position{};
        RECT client{};
        if (!GetCursorPos(&position) || !ScreenToClient(parameters.hFocusWindow, &position) || !GetClientRect(parameters.hFocusWindow, &client) || !PtInRect(&client, position))
            return;
        device->ShowCursor(FALSE);
        CGui::RenderCursor(position.x, position.y);
    }

    void RenderNativeTextDraws()
    {
        ZoneScoped;

        auto* pools = g_netGame ? g_netGame->m_pPools : nullptr;
        auto* textDrawPool = pools ? pools->m_pTextDraw : nullptr;

        // RenderWare's camera is still active here; Present runs after EndUpdate
        if (!textDrawPool || CGame::GetState() != 9 || CGame::IsPaused() || !CGame::HasFontResources())
            return;
        const bool paused = (g_input && g_input->IsEnabled()) || (g_dialog && g_dialog->IsActive()) || (g_scoreboard && g_scoreboard->m_bIsEnabled);
        if (g_textDrawSelection)
        {
            if (paused)
                g_textDrawSelection->ResetTextDraws();
            else
                g_textDrawSelection->Process();
        }
        textDrawPool->Draw();
        CGame::FlushFonts();
    }

    void SetVehicleDebugLabels(bool enabled)
    {
        g_vehicleDebugLabels = enabled;
    }

    void RenderUi(IDirect3DDevice9* device)
    {
        ZoneScoped;
        CGui::BeginFrame();
        if (g_shuttingDown)
            return;
#if defined(_DEBUG)
        UpdateFps();
#endif // _DEBUG                                                                                                       \
    // GS_PLAYING_GAME: skip splash screens, frontend and loading.
        if (CGame::GetState() != 9)
            return;

        if (FAILED(device->TestCooperativeLevel()))
            return;

        auto* pools = g_netGame ? g_netGame->m_pPools : nullptr;
        auto* vehiclePool = pools ? pools->m_pVehicle : nullptr;
        auto* objectPool = pools ? pools->m_pObject : nullptr;
        auto* menuPool = pools ? pools->m_pMenu : nullptr;
        auto* textDrawPool = pools ? pools->m_pTextDraw : nullptr;
        auto* labelPool = pools ? pools->m_pLabel : nullptr;
        auto* playerPool = pools ? pools->m_pPlayer : nullptr;

        {
            unsigned materialBudget = 1;
            if (objectPool)
                objectPool->ConstructMaterials(materialBudget);
            CObject::ConstructAllMaterialText(materialBudget);
            if (vehiclePool)
                vehiclePool->ConstructLicensePlates();
        }
        // model previews own a temporary RW camera; the game's camera has ended before Present
        if (textDrawPool)
            textDrawPool->LoadPreviews();

        // CMenuManager::m_bMenuActive: don't draw chat over the pause menu.
        if (CGame::IsPaused())
        {
            ProcessScreenshot(device);
            return;
        }

        if (FAILED(device->BeginScene()))
            return;

        const bool f5Pressed = (::GetAsyncKeyState(VK_F5) & 0x8000) != 0;
        const bool f10Pressed = (::GetAsyncKeyState(VK_F10) & 0x8000) != 0;

        const bool netStatsVisible = g_netStats && f5Pressed;
        const bool srvNetStatsActive = !f5Pressed && g_srvNetStats && f10Pressed;

        if (g_srvNetStats)
            g_srvNetStats->SetActive(srvNetStatsActive);

        const bool hideChatForStats = netStatsVisible || srvNetStatsActive;

        if (hideChatForStats)
        {
            if (g_chat->m_pScrollbar)
                g_chat->m_pScrollbar->SetVisible(false);
        }
        else
        {
            g_chat->UpdateScrollbarVisibility(g_input->IsEnabled());
        }

        DrawPlayerTags();
        if (g_chatBubble)
            g_chatBubble->Draw();
        DrawVehicleDebugLabels();
        if (g_cameraTargetLabels && playerPool && playerPool->GetLocalPlayer())
            playerPool->GetLocalPlayer()->DrawCameraTargetLabel();
        if (labelPool)
            labelPool->Draw();
        if (!hideChatForStats)
            g_chat->Draw();
        if (g_input->IsEnabled())
            g_input->UpdateLayout();
        if (g_dialog && g_dialog->m_pDialog)
            g_dialog->m_pDialog->SetVisible(g_dialog->IsActive() && !g_input->IsEnabled() && !(g_scoreboard && g_scoreboard->m_bIsEnabled));
        if (g_scoreboard && g_scoreboard->m_bIsEnabled)
        {
            g_scoreboard->Recalc();
            g_scoreboard->UpdateList();
            if (g_scoreboard->m_pDialog)
                g_scoreboard->m_pDialog->SetVisible(!g_input->IsEnabled());
        }
        if (g_deathWindow)
            g_deathWindow->Draw();
        UpdateClassSelection(device);
#if defined(LEGACY_SPAWN_SCREEN)
        if (g_spawnScreen && g_spawnScreen->m_bEnabled)
            g_spawnScreen->Draw();
#endif // LEGACY_SPAWN_SCREEN
        CGui::Render();
        if (g_scoreboard && !g_input->IsEnabled())
            g_scoreboard->Draw();
        if (g_dialog)
            g_dialog->Draw();
        if (g_downloadWindow)
            g_downloadWindow->Draw();
        const bool objectUiPaused = g_input->IsEnabled() || g_dialog->IsActive() || g_scoreboard->m_bIsEnabled;
        if (g_objectEdit)
        {
            g_objectEdit->SetPaused(objectUiPaused);
            g_objectEdit->Render();
        }
        if (g_objectSelection && !objectUiPaused)
            g_objectSelection->Draw();
        if (menuPool && !objectUiPaused)
            menuPool->Process();
        if (g_audioStream)
            g_audioStream->DrawInfo();
        if (netStatsVisible)
            g_netStats->Draw();
        if (srvNetStatsActive)
            g_srvNetStats->Draw();
        RenderCursor(device);
#if defined(_DEBUG)
        g_fonts->DrawFps(g_fps, g_frameTimeMs);
#endif // _DEBUG
        device->EndScene();
        ProcessScreenshot(device);
    }

    void LostUiDevice(IDirect3DDevice9*)
    {
        ZoneScoped;
        auto* pools = g_netGame ? g_netGame->m_pPools : nullptr;
        auto* vehiclePool = pools ? pools->m_pVehicle : nullptr;
        auto* objectPool = pools ? pools->m_pObject : nullptr;
        auto* textDrawPool = pools ? pools->m_pTextDraw : nullptr;

        if (g_shuttingDown)
            return;
        if (g_playerTags)
            g_playerTags->OnLostDevice();
        if (g_label)
            g_label->OnLostDevice();
        if (objectPool)
            objectPool->ShutdownMaterials();
        if (vehiclePool)
            vehiclePool->ShutdownLicensePlates();
        if (g_objectMaterialText)
            g_objectMaterialText->OnLostDevice();
        if (g_licensePlate)
            g_licensePlate->OnLostDevice();
        if (textDrawPool)
            textDrawPool->UnloadPreviews();
        if (g_objectEdit)
            g_objectEdit->OnLostDevice();
        if (g_deathWindow)
            g_deathWindow->OnLostDevice();
#if defined(LEGACY_SPAWN_SCREEN)
        if (g_spawnScreen)
            g_spawnScreen->OnLostDevice();
#endif // LEGACY_SPAWN_SCREEN
        CGui::OnLostDevice();
        if (g_chat)
            g_chat->OnLostDevice();
        if (g_render)
            g_render->OnLostDevice();
        if (g_fonts)
            g_fonts->OnLostDevice();
    }

    void ResetUiDevice(IDirect3DDevice9*, const D3DPRESENT_PARAMETERS&)
    {
        ZoneScoped;
        if (g_shuttingDown)
            return;
        if (g_objectMaterialText)
            g_objectMaterialText->OnResetDevice();
        if (g_licensePlate)
            g_licensePlate->OnResetDevice();
        if (g_fonts && !g_fonts->Reset())
        {
#if defined(_DEBUG)
            Log("failed to recreate fonts!");
#endif // _DEBUG
            ExitProcess(1);
        }
        if (g_objectEdit)
            g_objectEdit->OnResetDevice();
        if (g_deathWindow)
            g_deathWindow->OnResetDevice();
#if defined(LEGACY_SPAWN_SCREEN)
        if (g_spawnScreen)
            g_spawnScreen->OnResetDevice();
#endif // LEGACY_SPAWN_SCREEN
        if (g_render)
            g_render->OnResetDevice();
        if (g_playerTags)
            g_playerTags->OnResetDevice();
        if (g_label)
            g_label->OnResetDevice();
        if (g_chat)
            g_chat->OnResetDevice();
        CGui::OnResetDevice();
        if (g_dialog)
            g_dialog->UpdateRects();
        if (CGui::IsInitialized() && !CGui::GetCursorTexture())
        {
#if defined(_DEBUG)
            Log("failed to recreate GUI cursor!");
#endif // _DEBUG
            ExitProcess(1);
        }
        if (g_input)
        {
            g_input->UpdateLayout();
            g_input->EnableCursor();
        }
        if (g_classSelectionVisible)
            SetCursorMode(2, false);
        if (g_textDrawSelection && g_textDrawSelection->m_bIsActive)
            SetCursorMode(2, false);
        if (g_dialog && g_dialog->IsActive())
            SetCursorMode(2, false);
        if (g_scoreboard && g_scoreboard->m_bIsEnabled)
        {
            SetCursorMode(3, false);
        }
        if ((g_objectEdit && g_objectEdit->m_bEnabled) || (g_objectSelection && g_objectSelection->m_bIsActive))
            SetCursorMode(2, false);
    }
}

namespace render
{
    bool ResolvePlayerLabel(std::uint16_t id, CVector& position, float& localDistance, float& cameraDistance)
    {
        ZoneScoped;
        auto* pools = g_netGame ? g_netGame->m_pPools : nullptr;
        auto* playerPool = pools ? pools->m_pPlayer : nullptr;

        auto* player = playerPool ? playerPool->GetPlayer(id) : nullptr;
        auto* ped = player ? player->m_pPed : nullptr;
        if (!ped || !ped->DoesExist() || !ped->IsOnScreen())
            return false;
        ped->GetBonePosition(8, &position);
        localDistance = ped->GetDistanceToLocalPlayer();
        cameraDistance = ped->GetDistanceToCamera();
        return true;
    }

    void DrawPlayerTags()
    {
        ZoneScoped;
        auto* pools = g_netGame ? g_netGame->m_pPools : nullptr;
        auto* playerPool = pools ? pools->m_pPlayer : nullptr;

        auto* settings = g_netGame ? g_netGame->m_pSettings : nullptr;
        if (!playerPool || !g_playerTags || !settings || !settings->m_bNameTags)
            return;
        bool hideStatus = false;
        g_config.GetParam(CConfig::PARAM_NO_NAMETAG_STATUS, hideStatus);
        g_playerTags->BeginLabel();
        for (int id = 0; id <= playerPool->m_nLargestId; ++id)
        {
            auto* player = playerPool->GetPlayer(static_cast<CPlayerPool::ID>(id));
            if (!player || !player->m_bDrawLabels)
                continue;
            CVector position{};
            float localDistance = 0, cameraDistance = 0;
            if (!ResolvePlayerLabel(static_cast<CPlayerPool::ID>(id), position, localDistance, cameraDistance) || localDistance > settings->m_fNameTagsDrawDist)
                continue;
            if (settings->m_bNoNametagsBehindWalls && (!g_label || !g_label->HasNoObstacles(position)))
                continue;
            const char* name = playerPool->GetName(static_cast<CPlayerPool::ID>(id));
            char text[512]{};
            std::snprintf(text, sizeof(text), "%s (%d)", name ? name : "", id);
            g_playerTags->DrawTag(&position, text, player->GetColorAsARGB(), player->m_fReportedHealth, player->m_fReportedArmour, cameraDistance, !hideStatus,
                                  player->GetStatus());
        }
        g_playerTags->EndLabel();
    }
}
