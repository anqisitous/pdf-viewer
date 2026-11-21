cmake_minimum_required(VERSION 3.20)
project(pdf_viewer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL3 REQUIRED IMPORTED_TARGET sdl3)

add_executable(pdf-viewer
    src/main.cpp
    src/book.cpp
    src/demo_source.cpp
    src/renderer.cpp
)

target_link_libraries(pdf-viewer PRIVATE PkgConfig::SDL3)
target_compile_options(pdf-viewer PRIVATE -Wall -Wextra)

if(APPLE)
    enable_language(OBJCXX)
    target_sources(pdf-viewer PRIVATE src/pdf_source_macos.mm)
    set_source_files_properties(src/pdf_source_macos.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
    target_link_libraries(pdf-viewer PRIVATE
        "-framework CoreGraphics"
        "-framework Foundation"
    )
else()
    target_sources(pdf-viewer PRIVATE src/pdf_source_stub.cpp)
endif()
