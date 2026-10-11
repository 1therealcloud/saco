add_executable(saa
    ../../source/archive/bindat.cpp
    ../../source/archive/saa.cpp
    ../../source/archive/saa_crypto.cpp
    
    saa_tool.cpp
    saa_pack.cpp
)

target_include_directories(saa PRIVATE ${PROJECT_SOURCE_DIR}/source)

target_link_libraries(saa PRIVATE advapi32)
