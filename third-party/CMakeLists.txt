add_subdirectory(minhook)

add_subdirectory(raknet)

add_library(stb INTERFACE)
target_include_directories(stb SYSTEM INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/stb)

add_library(bass SHARED IMPORTED GLOBAL)
set_target_properties(bass PROPERTIES
    IMPORTED_IMPLIB "${CMAKE_CURRENT_SOURCE_DIR}/bass/bass.lib"
    IMPORTED_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/bass/bass.dll"
    INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/bass"
)
