cmake_minimum_required(VERSION 3.16)

project(blp_qt_gui LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 COMPONENTS Widgets QUIET)
if (Qt6_FOUND)
    set(QT_PACKAGE Qt6)
    qt_standard_project_setup()
else()
    find_package(Qt5 COMPONENTS Widgets REQUIRED)
    set(QT_PACKAGE Qt5)
endif()

add_executable(blp_viewer
    src/main.cpp
    src/mainwindow.cpp
    src/image_io.cpp
    src/blp_api.cpp
    src/imageview.cpp
)

option(BLP_STATIC_LINK "Link BLP library statically" OFF)

target_include_directories(blp_viewer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

target_link_libraries(blp_viewer PRIVATE ${QT_PACKAGE}::Widgets)

if (BLP_STATIC_LINK)
    target_compile_definitions(blp_viewer PRIVATE BLP_STATIC_LINK)

    if (WIN32)
        set(BLP_STATIC_LIB "${CMAKE_CURRENT_SOURCE_DIR}/../target/release/blp_lib.lib")
    else()
        set(BLP_STATIC_LIB "${CMAKE_CURRENT_SOURCE_DIR}/../target/release/libblp_lib.a")
    endif()

    if (EXISTS "${BLP_STATIC_LIB}")
        target_link_libraries(blp_viewer PRIVATE "${BLP_STATIC_LIB}")
        if (WIN32)
            target_link_libraries(blp_viewer PRIVATE ws2_32 ntdll)
        endif()
    else()
        message(FATAL_ERROR "BLP static library not found: ${BLP_STATIC_LIB}. Build it with cargo build --release.")
    endif()
endif()

if (WIN32)
    add_library(blp_thumbnail SHARED
        shell_ext/blp_thumbnail.cpp
        shell_ext/blp_thumbnail.def
    )

    target_include_directories(blp_thumbnail PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
    )

    target_link_libraries(blp_thumbnail PRIVATE
        ole32
        shell32
        shlwapi
        gdi32
    )

    set_target_properties(blp_thumbnail PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/$<CONFIG>
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/$<CONFIG>
    )
endif()
