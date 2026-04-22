cmake_minimum_required(VERSION 3.20)
project(assurance-forge LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- ImGui ---
set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/external/imgui)
add_library(imgui STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
    ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp
)
target_include_directories(imgui PUBLIC
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)
target_link_libraries(imgui PUBLIC d3d11 d3dcompiler dxgi)

# --- pugixml ---
add_subdirectory(external/pugixml)

# --- Main application ---
add_executable(assurance-forge WIN32
    src/app/main.cpp
    src/app/app_runtime.cpp
    src/app/platform_win32_dx11.cpp
    src/parser/xml_parser.cpp
    src/ui/gsn_canvas.cpp
    src/ui/gsn_layout.cpp
    src/ui/gsn_canvas_renderer.cpp
    src/ui/gsn_adapter.cpp
    src/ui/tree_view.cpp
    src/ui/element_panel.cpp
    src/ui/register_views.cpp
    src/ui/sccg_panel.cpp
    src/ui/ui_state.cpp
    src/core/app_state.cpp
    src/core/sccg_review.cpp
    src/core/assurance_tree.cpp
    src/sacm/sacm_parser.cpp
    src/sacm/sacm_serializer.cpp
)
target_include_directories(assurance-forge PRIVATE src)
target_link_libraries(assurance-forge PRIVATE imgui pugixml)

# --- Tests ---
include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.16.0
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()
add_executable(tests
    tests/test_xml_parser.cpp
    tests/test_assurance_tree.cpp
    tests/test_layout.cpp
    tests/test_sacm_roundtrip.cpp
    src/parser/xml_parser.cpp
    src/core/assurance_tree.cpp
    src/ui/gsn_layout.cpp
    src/sacm/sacm_parser.cpp
    src/sacm/sacm_serializer.cpp
)
target_include_directories(tests PRIVATE src)
target_link_libraries(tests PRIVATE GTest::gtest_main pugixml imgui)
include(GoogleTest)
gtest_discover_tests(tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
