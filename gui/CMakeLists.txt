cmake_minimum_required(VERSION 3.16)

project(blp_viewer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- ImGui sources ---
set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui)
set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/imgui_impl_win32.cpp
    ${IMGUI_DIR}/imgui_impl_dx11.cpp
)

# --- Application sources ---
set(BLP_VIEWER_SOURCES
    src/main.cpp
    src/app.cpp
    src/style.cpp
    src/utils.cpp
    src/image_io.cpp
    src/image_ops.cpp
    src/blp_api.cpp
    src/image_texture.cpp
    src/image_view.cpp
    src/file_dialogs.cpp
    src/settings.cpp
    src/win_integration.cpp
    src/ui_main.cpp
    src/ui_left_panel.cpp
    src/ui_right_panel.cpp
    src/blp_viewer.rc
)

add_executable(blp_viewer WIN32
    ${BLP_VIEWER_SOURCES}
    ${IMGUI_SOURCES}
)

option(BLP_STATIC_LINK "Link BLP library statically" OFF)

target_include_directories(blp_viewer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

target_compile_definitions(blp_viewer PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    UNICODE
    _UNICODE
)

if (MSVC)
    target_compile_options(blp_viewer PRIVATE /utf-8)
endif()

target_link_libraries(blp_viewer PRIVATE
    d3d11
    d3dcompiler
    dxgi
    dwmapi
    ole32
    comdlg32
    shell32
    shlwapi
    gdi32
)

if (BLP_STATIC_LINK)
    target_compile_definitions(blp_viewer PRIVATE BLP_STATIC_LINK)

    set(BLP_STATIC_LIB "${CMAKE_CURRENT_SOURCE_DIR}/../target/release/blp_lib.lib")

    if (EXISTS "${BLP_STATIC_LIB}")
        target_link_libraries(blp_viewer PRIVATE "${BLP_STATIC_LIB}")
        target_link_libraries(blp_viewer PRIVATE ws2_32 ntdll)
    else()
        message(FATAL_ERROR "BLP static library not found: ${BLP_STATIC_LIB}. Build it with cargo build --release.")
    endif()
endif()

# --- blp_thumbnail shell extension (unchanged) ---
add_library(blp_thumbnail SHARED
    shell_ext/blp_thumbnail.cpp
    shell_ext/blp_thumbnail.def
)

target_include_directories(blp_thumbnail PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb
)

target_link_libraries(blp_thumbnail PRIVATE
    ole32
    shell32
    shlwapi
    gdi32
)

if (BLP_STATIC_LINK)
    target_compile_definitions(blp_thumbnail PRIVATE BLP_STATIC_LINK)

    if (EXISTS "${BLP_STATIC_LIB}")
        target_link_libraries(blp_thumbnail PRIVATE "${BLP_STATIC_LIB}")
        target_link_libraries(blp_thumbnail PRIVATE ws2_32 ntdll)
    else()
        message(FATAL_ERROR "BLP static library not found: ${BLP_STATIC_LIB}. Build it with cargo build --release.")
    endif()
endif()

set_target_properties(blp_thumbnail PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/$<CONFIG>
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/$<CONFIG>
)
