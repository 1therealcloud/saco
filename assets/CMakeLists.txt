find_program(MAKENSIS_EXECUTABLE NAMES makensis
    HINTS "$ENV{ProgramFiles}/NSIS" "$ENV{SystemDrive}/Program Files (x86)/NSIS"
)

if(NOT MAKENSIS_EXECUTABLE)
    message(STATUS "NSIS not found; installer target is unavailable")
    return()
endif()

file(GLOB_RECURSE payload_files CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/payload/*")
set(installer_dir "${PROJECT_BINARY_DIR}/installer/$<CONFIG>")
set(installer_file "${installer_dir}/sa-mp-0.3.DL-R1-install.exe")

add_custom_command(
    OUTPUT "${installer_file}"
    COMMAND ${CMAKE_COMMAND} -E $<IF:$<CONFIG:Release>,true,false>
    COMMAND ${CMAKE_COMMAND} -E make_directory "${installer_dir}/payload"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/payload" "${installer_dir}/payload"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/samp.exe"
        $<TARGET_FILE:samp>
        "${PROJECT_SOURCE_DIR}/third-party/bass/bass.dll"
        $<TARGET_FILE:samp_debug>
        $<TARGET_FILE:rcon>
        "${installer_dir}/payload"
    COMMAND "${MAKENSIS_EXECUTABLE}" /V2
        "/DPAYLOAD_DIR=${installer_dir}/payload"
        "/DOUTPUT_FILE=${installer_file}"
        "${CMAKE_CURRENT_SOURCE_DIR}/samp-0.3.DL-R1.nsi"
    DEPENDS samp samp_debug rcon "${PROJECT_SOURCE_DIR}/third-party/bass/bass.dll"
        ${payload_files}
        samp.exe modern-wizard.bmp samp-0.3.DL-R1.nsi CMakeLists.txt
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Building Release installer"
    VERBATIM
)

add_custom_target(installer DEPENDS "${installer_file}")
