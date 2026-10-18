# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# The original code is copyright (c) 2026, 1therealcloud.

if (MSVC)
    # library debug data only in Debug
    foreach(language C CXX)
        string(REGEX REPLACE "(^|[ \t])/(Zi|ZI|Z7)([ \t]|$)" " "
            CMAKE_${language}_FLAGS_RELWITHDEBINFO "${CMAKE_${language}_FLAGS_RELWITHDEBINFO}")
    endforeach()
    set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<$<CONFIG:Debug>:ProgramDatabase>")
endif()

add_subdirectory(directxmath EXCLUDE_FROM_ALL)

add_subdirectory(minhook)

add_subdirectory(raknet)

set(TRACY_ENABLE ${SAMP_TRACY} CACHE BOOL "Enable Tracy profiling" FORCE)
set(TRACY_ON_DEMAND ON CACHE BOOL "Collect Tracy events only while connected" FORCE)
set(TRACY_NO_CRASH_HANDLER ON CACHE BOOL "Keep the client exception handler" FORCE)
add_subdirectory(tracy EXCLUDE_FROM_ALL)

add_library(stb INTERFACE)
target_include_directories(stb SYSTEM INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/stb)

add_library(bass SHARED IMPORTED GLOBAL)
set_target_properties(bass PROPERTIES
    IMPORTED_IMPLIB "${CMAKE_CURRENT_SOURCE_DIR}/bass/bass.lib"
    IMPORTED_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/bass/bass.dll"
    INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/bass"
)

# Keep libcurl self-contained, using Windows TLS and the dynamic MSVC runtime.
function(add_samp_curl)
    set(BUILD_SHARED_LIBS OFF)
    set(BUILD_STATIC_LIBS ON)
    set(BUILD_CURL_EXE OFF)
    set(BUILD_TESTING OFF)
    set(CURL_DISABLE_INSTALL ON)
    set(CURL_STATIC_CRT OFF)
    set(HTTP_ONLY ON)
    set(CURL_USE_SCHANNEL ON)
    set(CURL_USE_OPENSSL OFF)
    set(CURL_USE_LIBPSL OFF)
    set(USE_LIBIDN2 OFF)
    set(USE_NGHTTP2 OFF)
    set(CURL_ZLIB OFF)
    set(CURL_BROTLI OFF)
    set(CURL_ZSTD OFF)
    add_subdirectory(curl EXCLUDE_FROM_ALL)
endfunction()
add_samp_curl()

add_library(lzma STATIC lzma/C/LzmaDec.c)
target_include_directories(lzma SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/lzma/C)
