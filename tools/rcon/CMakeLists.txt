# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# The original code is copyright (c) 2026, 1therealcloud.


add_executable(rcon rcon.cpp)

target_include_directories(rcon PRIVATE ${PROJECT_SOURCE_DIR}/source)
target_precompile_headers(rcon PRIVATE ${PROJECT_SOURCE_DIR}/source/stdafx.h)
target_link_libraries(rcon PRIVATE ws2_32 minhook Tracy::TracyClient)

if(MSVC)
    target_compile_options(rcon PRIVATE /W4 /permissive- /utf-8)
endif()
