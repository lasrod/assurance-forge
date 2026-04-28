cmake_minimum_required(VERSION 3.20)
project(assurance-forge LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/FS)
endif()

set(HELLOIMGUI_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(HELLOIMGUI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(HELLOIMGUI_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(HELLOIMGUI_USE_FREETYPE OFF CACHE BOOL "" FORCE)
set(HELLOIMGUI_USE_GLFW3 ON CACHE BOOL "" FORCE)
set(HELLOIMGUI_HAS_OPENGL3 ON CACHE BOOL "" FORCE)
add_subdirectory(external/hello_imgui)

set(NFD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(NFD_BUILD_SDL2_TESTS OFF CACHE BOOL "" FORCE)
set(NFD_BUILD_GLFW3_TESTS OFF CACHE BOOL "" FORCE)
set(NFD_INSTALL OFF CACHE BOOL "" FORCE)
set(NFD_WAYLAND OFF CACHE BOOL "" FORCE)
add_subdirectory(external/nativefiledialog-extended)

set(ASSURANCE_FORGE_IMGUI_INCLUDE_DIRS
    ${CMAKE_SOURCE_DIR}/external/hello_imgui/external/imgui
    ${CMAKE_SOURCE_DIR}/external/hello_imgui/external/imgui/backends
)

# --- pugixml ---
add_subdirectory(external/pugixml)

set(ASSURANCE_FORGE_SHARED_SOURCES
    src/parser/xml_parser.cpp
    src/ui/panels/welcome_modal.cpp
    src/ui/panels/element_panel.cpp
    src/ui/panels/project_files_panel.cpp
    src/ui/panels/sacm_viewer_panel.cpp
    src/ui/widgets/splitter.cpp
    src/ui/gsn/gsn_canvas.cpp
    src/ui/gsn/gsn_layout.cpp
    src/ui/gsn/gsn_canvas_renderer.cpp
    src/ui/gsn/gsn_adapter.cpp
    src/ui/element_context_menu.cpp
    src/ui/tree_view.cpp
    src/ui/register_views.cpp
    src/ui/ui_state.cpp
    src/ui/theme.cpp
    src/core/app_state.cpp
    src/core/assurance_tree.cpp
    src/core/element_factory.cpp
    src/core/project_model.cpp
    src/core/project_service.cpp
    src/sacm/sacm_parser.cpp
    src/sacm/sacm_serializer.cpp
)

set(ASSURANCE_FORGE_APP_SOURCES
    src/app/main.cpp
    src/app/app_ui_bootstrap.cpp
    src/app/app_runtime.cpp
    src/app/native_file_dialogs.cpp
    ${ASSURANCE_FORGE_SHARED_SOURCES}
)

# --- Main application ---
hello_imgui_add_app(assurance-forge ${ASSURANCE_FORGE_APP_SOURCES})
target_include_directories(assurance-forge PRIVATE src ${ASSURANCE_FORGE_IMGUI_INCLUDE_DIRS})
target_link_libraries(assurance-forge PRIVATE imgui pugixml nfd)

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
    tests/test_element_factory.cpp
    tests/test_project_service.cpp
    src/parser/xml_parser.cpp
    src/core/assurance_tree.cpp
    src/core/element_factory.cpp
    src/core/project_model.cpp
    src/core/project_service.cpp
    src/ui/gsn/gsn_layout.cpp
    src/sacm/sacm_parser.cpp
    src/sacm/sacm_serializer.cpp
)
target_include_directories(tests PRIVATE src ${ASSURANCE_FORGE_IMGUI_INCLUDE_DIRS})
target_link_libraries(tests PRIVATE GTest::gtest_main pugixml imgui)
include(GoogleTest)
gtest_discover_tests(tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
