cmake_minimum_required(VERSION 3.20)
project(nes_emulator LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL3 REQUIRED IMPORTED_TARGET sdl3)

add_executable(nes
    src/main.cpp
    src/cartridge.cpp
    src/cpu.cpp
    src/ppu.cpp
    src/bus.cpp
    src/controller.cpp
    src/apu.cpp
)

target_include_directories(nes PRIVATE src ${SDL3_INCLUDE_DIRS})
target_link_libraries(nes PRIVATE PkgConfig::SDL3)
