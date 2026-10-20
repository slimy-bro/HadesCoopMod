add_library(HadesCoopGame SHARED)

add_definitions(-DEASTL_DEBUG=0)
add_definitions(-DEASTL_CUSTOM_FLOAT_CONSTANTS_REQUIRED)

include_directories (
   src/.
   ../include
   ../libs/lua-5.2.2/src
)

file(GLOB_RECURSE MOD_FILES src/*.cpp src/*.h)

target_sources(
    HadesCoopGame
    PUBLIC
        ${MOD_FILES}
)

target_compile_definitions(HadesCoopGame PRIVATE HADES_COOP_VERSION="${HadesCoop_VERSION}")

target_link_libraries(HadesCoopGame PRIVATE liblua)
target_precompile_headers(HadesCoopGame PRIVATE src/pch.h)
