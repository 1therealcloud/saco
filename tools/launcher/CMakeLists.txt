# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# The original code is copyright (c) 2026, 1therealcloud.


add_executable(samp_debug WIN32
    launcher.cpp
    launcher.manifest
    resources.rc
)

target_include_directories(samp_debug PRIVATE ${PROJECT_SOURCE_DIR}/source)
target_precompile_headers(samp_debug PRIVATE ${PROJECT_SOURCE_DIR}/source/stdafx.h)
target_compile_definitions(samp_debug PRIVATE UNICODE _UNICODE)
target_link_libraries(samp_debug PRIVATE user32 minhook Tracy::TracyClient)

if(MSVC)
    target_compile_options(samp_debug PRIVATE /W4 /permissive- /utf-8)
endif()
