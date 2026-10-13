# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# The original code is copyright (c) 2026, 1therealcloud.

add_executable(saa
    ../../source/archive/bindat.cpp
    ../../source/archive/saa.cpp
    ../../source/archive/saa_crypto.cpp
    
    saa_tool.cpp
    saa_pack.cpp
)

target_include_directories(saa PRIVATE ${PROJECT_SOURCE_DIR}/source)

target_precompile_headers(saa PRIVATE ${PROJECT_SOURCE_DIR}/source/stdafx.h)
target_link_libraries(saa PRIVATE advapi32 minhook Tracy::TracyClient)
