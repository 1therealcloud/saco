# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# The original code is copyright (c) 2026, 1therealcloud.

# cmake -B build -A Win32; cmake --build build --config Release

cmake_minimum_required(VERSION 3.21)
project(saco LANGUAGES C CXX)

# only Win32 is supported
if (NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "only Win32 is supported")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(SAMP_TRACY "Enable Tracy profiling" OFF)
option(SAMP_TRACY_OBJECTS "Enable per-object Tracy zones" OFF)

include(CheckIPOSupported)
check_ipo_supported(RESULT SAMP_IPO_SUPPORTED OUTPUT SAMP_IPO_ERROR LANGUAGES C CXX)
if (NOT SAMP_IPO_SUPPORTED)
    message(FATAL_ERROR "Release LTO is unavailable: ${SAMP_IPO_ERROR}")
endif()

if (MSVC)
    string(REPLACE "/Ob2" "/Ob3" CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE}")
    string(REPLACE "/Ob2" "/Ob3" CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}")
endif()

# enable IPO for older dependencies
set(CMAKE_POLICY_DEFAULT_CMP0069 NEW)

add_subdirectory(third-party)

set(RENDERWARE_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/source/game/RenderWare/D3DIndexDataBuffer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/source/game/RenderWare/D3DResourceSystem.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/source/game/RenderWare/D3DTextureBuffer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/source/game/RenderWare/rw/rphanim.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/source/game/RenderWare/rw/rpskin.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/source/game/RenderWare/rw/rpworld.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/source/game/RenderWare/rw/rwcore.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/source/game/RenderWare/rw/rwplcore.cpp
)

add_library(samp SHARED
    ${RENDERWARE_SOURCES}

    source/archive/bindat.cpp
    source/archive/saa.cpp
    source/archive/saa_crypto.cpp
    source/audio/CAudioStream.cpp
    source/config/CConfig.cpp
    source/config/CSettings.cpp
    source/custommodels/CCustomModels.cpp
    source/custommodels/CCustomModelsPool.cpp
    source/custommodels/CDownloadedFilesPool.cpp
    source/dllmain.cpp
    source/exceptbox.rc
    source/exceptions.cpp
    source/game/BuildingRemoval.cpp
    source/game/CAudio.cpp
    source/game/CCamera.cpp
    source/game/CGame.cpp
    source/game/Cursor.cpp
    source/game/d3d9hook.cpp
    source/game/Debug.cpp
    source/game/entities/CActor.cpp
    source/game/entities/CEntity.cpp
    source/game/entities/CObject.cpp
    source/game/entities/CPed.cpp
    source/game/entities/CVehicle.cpp
    source/game/FileSystem.cpp
    source/game/InputHandler.cpp
    source/game/ModelInfo.cpp
    source/game/patch/base.cpp
    source/game/patch/FrameLimiter.cpp
    source/game/patch/hooks.cpp
    source/game/patch/HudScaleFix.cpp
    source/game/patch/modelinfo.cpp
    source/game/patch/patch.cpp
    source/game/patch/pools.cpp
    source/game/patch/streaming.cpp
    source/game/PlayerSlots.cpp
    source/game/render/CLicensePlate.cpp
    source/game/render/CObjectMaterialText.cpp
    source/game/render/model_preview.cpp
    source/game/render/models.cpp
    source/game/render/textures.cpp
    source/game/Scripting.cpp
    source/gui/CChat.cpp
    source/gui/CChatBubble.cpp
    source/gui/CDeathWindow.cpp
    source/gui/CDialog.cpp
    source/gui/CDownloadWindow.cpp
    source/gui/CGui.cpp
    source/gui/CHelpDialog.cpp
    source/gui/CInput.cpp
    source/gui/CLabel.cpp
    source/gui/CMenu.cpp
    source/gui/CObjectEdit.cpp
    source/gui/CObjectSelection.cpp
    source/gui/Commands.cpp
    source/gui/CPlayerTags.cpp
    source/gui/CScoreboard.cpp
    source/gui/CSpawnScreen.cpp
    source/gui/CTextDraw.cpp
    source/gui/CTextDrawSelection.cpp
    source/gui/ime.cpp
    source/main.cpp
    source/net/authentication.cpp
    source/net/CHttpClient.cpp
    source/net/CNetGame.cpp
    source/net/CNetStats.cpp
    source/net/CSrvNetStats.cpp
    source/net/RPCHandlers.cpp
    source/players/CLocalPlayer.cpp
    source/players/CPlayerInfo.cpp
    source/players/CRemotePlayer.cpp
    source/players/PlayerColors.cpp
    source/pools/CActorPool.cpp
    source/pools/CGangZonePool.cpp
    source/pools/CLabelPool.cpp
    source/pools/CMenuPool.cpp
    source/pools/CObjectPool.cpp
    source/pools/CPickupPool.cpp
    source/pools/CPlayerPool.cpp
    source/pools/CTextDrawPool.cpp
    source/pools/CVehiclePool.cpp
    source/render/CFonts.cpp
    source/render/CRender.cpp
    source/render/render.cpp
    source/render/screenshot.cpp
    source/sync/AimStuff.cpp
    source/sync/BulletSync.cpp
    source/sync/KeyStuff.cpp
    source/utils/math/CMatrix.cpp
    source/utils/math/CQuaternion.cpp
    source/utils/math/CRect.cpp
    source/utils/math/CVector.cpp
    source/utils/math/CVector2D.cpp
)

target_include_directories(samp PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/source
    ${CMAKE_CURRENT_SOURCE_DIR}/source/game/RenderWare/rw
)

target_precompile_headers(samp PRIVATE
    source/stdafx.h
)

target_link_libraries(samp PRIVATE
    DirectXMath
    lzma
    CURL::libcurl
    bass
    minhook
    raknet
    stb
    Tracy::TracyClient

    d3d9

    #ws2_32
    imm32
    advapi32
    gdi32
    windowscodecs
    ole32
    shell32
    dbghelp
    dwmapi
)

add_subdirectory(tools)

foreach(target samp minhook raknet lzma libcurl_object libcurl_static TracyClient img saa)
    set_property(TARGET ${target} PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
    if (MSVC)
        target_compile_options(${target} PRIVATE "$<$<CONFIG:Release>:/Gw>" "$<$<CONFIG:Release>:/Oy->")
    endif()
endforeach()

if (MSVC)
    target_link_options(samp PRIVATE "$<$<CONFIG:Release>:/LTCG>" "$<$<CONFIG:Release>:/OPT:REF>" "$<$<CONFIG:Release>:/OPT:ICF>")
endif()

#add_custom_command(TARGET samp POST_BUILD
#    COMMAND ${CMAKE_COMMAND} -E copy_if_different
#        $<TARGET_FILE:bass> $<TARGET_FILE_DIR:samp>/bass.dll
#)

if(SAMP_TRACY_OBJECTS)
    target_compile_definitions(samp PRIVATE SAMP_TRACY_OBJECTS)
endif()
