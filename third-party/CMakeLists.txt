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
    #IMPORTED_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/bass/bass.dll"
    INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/bass"
)
