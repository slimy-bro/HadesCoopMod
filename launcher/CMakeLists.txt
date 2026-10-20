add_executable(HadesCoopLauncher WIN32 src/main.cpp)

target_compile_definitions(HadesCoopLauncher PRIVATE UNICODE _UNICODE)
target_link_libraries(HadesCoopLauncher PRIVATE shell32)
