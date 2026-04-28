cmake_minimum_required(VERSION 3.21)
project(assurance-forge LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(BUILD_SHARED_LIBS "Build shared libraries" OFF)

include(FetchContent)

# --- JSON ---
FetchContent_Declare(
    nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

# --- libcurl ---
find_package(CURL QUIET)
if(NOT CURL_FOUND)
    set(BUILD_CURL_EXE OFF CACHE BOOL "" FORCE)
    set(CURL_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BUILD_LIBCURL_DOCS OFF CACHE BOOL "" FORCE)
    set(ENABLE_CURL_MANUAL OFF CACHE BOOL "" FORCE)
    if(WIN32)
        set(CURL_USE_SCHANNEL ON CACHE BOOL "" FORCE)
        set(CURL_WINDOWS_SSPI ON CACHE BOOL "" FORCE)
    endif()
    FetchContent_Declare(
        curl
        URL https://github.com/curl/curl/releases/download/curl-8_9_1/curl-8.9.1.tar.xz
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(curl)
endif()

if(CURL_FOUND AND NOT TARGET CURL::libcurl)
    add_library(CURL::libcurl UNKNOWN IMPORTED)
    set_target_properties(CURL::libcurl PROPERTIES
        IMPORTED_LOCATION "${CURL_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${CURL_INCLUDE_DIRS}"
        INTERFACE_LINK_LIBRARIES "${CURL_LIBRARIES}"
    )
endif()
if(NOT TARGET CURL::libcurl AND TARGET libcurl)
    add_library(CURL::libcurl ALIAS libcurl)
endif()
if(NOT TARGET CURL::libcurl AND TARGET libcurl_static)
    add_library(CURL::libcurl ALIAS libcurl_static)
endif()

# --- ImGui ---
set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/external/imgui)
add_library(imgui_core STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
)
target_include_directories(imgui_core PUBLIC
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)

add_library(imgui_backends STATIC
    ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
    ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp
)
target_link_libraries(imgui_backends PUBLIC imgui_core d3d11 d3dcompiler dxgi)

# --- pugixml ---
add_subdirectory(external/pugixml)

# --- Main application ---
add_executable(assurance-forge WIN32
    src/ai/ai_service.cpp
    src/ai/ai_settings.cpp
    src/ai/ai_task_runner.cpp
    src/ai/ai_types.cpp
    src/ai/http_client.cpp
    src/ai/libcurl_http_client.cpp
    src/ai/openai_provider.cpp
    src/ai/secret_store.cpp
    src/app/main.cpp
    src/app/app_runtime.cpp
    src/app/platform_win32_dx11.cpp
    src/parser/xml_parser.cpp
    src/ui/panels/welcome_modal.cpp
    src/ui/panels/element_panel.cpp
    src/ui/panels/preferences_panel.cpp
    src/ui/panels/problems_panel.cpp
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
    src/core/problems/problem_item.cpp
    src/core/problems/problems_manager.cpp
    src/core/project_model.cpp
    src/core/project_service.cpp
    src/sacm/sacm_parser.cpp
    src/sacm/sacm_serializer.cpp
)
target_include_directories(assurance-forge PRIVATE src)
target_link_libraries(assurance-forge PRIVATE imgui_core imgui_backends pugixml nlohmann_json::nlohmann_json CURL::libcurl bcrypt ole32 shell32)
if(WIN32)
    target_link_libraries(assurance-forge PRIVATE advapi32)
endif()

# --- Tests ---
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
    tests/test_problems_manager.cpp
    tests/test_sacm_roundtrip.cpp
    tests/test_element_factory.cpp
    tests/test_project_service.cpp
    tests/test_ai_settings.cpp
    tests/test_ai_service.cpp
    tests/test_openai_provider.cpp
    tests/test_ai_task_runner.cpp
    src/ai/ai_service.cpp
    src/ai/ai_settings.cpp
    src/ai/ai_task_runner.cpp
    src/ai/ai_types.cpp
    src/ai/http_client.cpp
    src/ai/openai_provider.cpp
    src/ai/secret_store.cpp
    src/parser/xml_parser.cpp
    src/core/assurance_tree.cpp
    src/core/element_factory.cpp
    src/core/problems/problem_item.cpp
    src/core/problems/problems_manager.cpp
    src/core/project_model.cpp
    src/core/project_service.cpp
    src/ui/gsn/gsn_layout.cpp
    src/sacm/sacm_parser.cpp
    src/sacm/sacm_serializer.cpp
)
target_include_directories(tests PRIVATE src)
target_link_libraries(tests PRIVATE GTest::gtest_main pugixml imgui_core nlohmann_json::nlohmann_json bcrypt)
if(WIN32)
    target_link_libraries(tests PRIVATE advapi32)

    add_custom_command(TARGET tests POST_BUILD
        COMMAND "$<$<BOOL:$<TARGET_RUNTIME_DLLS:tests>>:${CMAKE_COMMAND};-E;copy_if_different;$<TARGET_RUNTIME_DLLS:tests>;$<TARGET_FILE_DIR:tests>>"
        COMMAND_EXPAND_LISTS
    )
endif()
include(GoogleTest)
gtest_discover_tests(tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
