cmake_minimum_required(VERSION 3.21)
project(assurance-forge LANGUAGES C CXX)

if(APPLE)
    enable_language(OBJC OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/FS /utf-8)
endif()

# yaml-cpp 0.8.0 still declares cmake_minimum_required(VERSION 3.4).
# Raise the policy floor for fetched dependencies without patching their sources.
set(CMAKE_POLICY_VERSION_MINIMUM 3.10)

include(FetchContent)

# --- JSON ---
FetchContent_Declare(
    nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

set(HELLOIMGUI_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(HELLOIMGUI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(HELLOIMGUI_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(HELLOIMGUI_USE_FREETYPE OFF CACHE BOOL "" FORCE)
set(HELLOIMGUI_USE_GLFW3 ON CACHE BOOL "" FORCE)
set(HELLOIMGUI_HAS_OPENGL3 ON CACHE BOOL "" FORCE)
set(HELLOIMGUI_USE_EXTERNAL_JSON ON CACHE BOOL "" FORCE)
add_subdirectory(external/hello_imgui)

set(NFD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(NFD_BUILD_SDL2_TESTS OFF CACHE BOOL "" FORCE)
set(NFD_BUILD_GLFW3_TESTS OFF CACHE BOOL "" FORCE)
set(NFD_INSTALL OFF CACHE BOOL "" FORCE)
set(NFD_WAYLAND OFF CACHE BOOL "" FORCE)
add_subdirectory(external/nativefiledialog-extended)

set(PICOSHA2_TEST OFF CACHE BOOL "" FORCE)
set(PICOSHA2_EXAMPLE OFF CACHE BOOL "" FORCE)
add_subdirectory(external/picosha2)

set(ASSURANCE_FORGE_IMGUI_INCLUDE_DIRS
    ${CMAKE_SOURCE_DIR}/external/hello_imgui/external/imgui
    ${CMAKE_SOURCE_DIR}/external/hello_imgui/external/imgui/backends
)

option(BUILD_SHARED_LIBS "Build shared libraries" OFF)

# --- YAML ---
set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_CONTRIB OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    yaml-cpp
    GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
    GIT_TAG 0.8.0
)
FetchContent_MakeAvailable(yaml-cpp)

if(TARGET yaml-cpp::yaml-cpp)
    set(AF_YAML_CPP_TARGET yaml-cpp::yaml-cpp)
elseif(TARGET yaml-cpp)
    set(AF_YAML_CPP_TARGET yaml-cpp)
else()
    message(FATAL_ERROR "yaml-cpp target was not created")
endif()

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

# --- pugixml ---
add_subdirectory(external/pugixml)

set(AF_GUIDELINES_SOURCE_FILE "${CMAKE_SOURCE_DIR}/external/safety-case-core-guidelines/data/guidelines.yaml")
if(NOT EXISTS "${AF_GUIDELINES_SOURCE_FILE}")
    message(FATAL_ERROR "Missing ${AF_GUIDELINES_SOURCE_FILE}. Run git submodule update --init --recursive.")
endif()

function(af_copy_guidelines_data target_name)
    add_custom_command(TARGET ${target_name} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${target_name}>/data"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${AF_GUIDELINES_SOURCE_FILE}"
            "$<TARGET_FILE_DIR:${target_name}>/data/guidelines.yaml"
        VERBATIM
    )
endfunction()

set(ASSURANCE_FORGE_SHARED_SOURCES
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
    src/ui/localization.cpp
    src/ui/theme.cpp
    src/core/app_state.cpp
    src/core/assurance_tree.cpp
    src/core/element_factory.cpp
    src/core/problems/problem_item.cpp
    src/core/problems/problems_manager.cpp
    src/core/project_model.cpp
    src/core/project_service.cpp
    src/core/sha256.cpp
    src/sacm/sacm_parser.cpp
    src/sacm/sacm_serializer.cpp
)
set(ASSURANCE_FORGE_APP_SOURCES
    src/ai/ai_claim_review.cpp
    src/ai/ai_service.cpp
    src/ai/ai_settings.cpp
    src/ai/ai_task_runner.cpp
    src/ai/ai_types.cpp
    src/ai/http_client.cpp
    src/ai/libcurl_http_client.cpp
    src/ai/openai_provider.cpp
    src/ai/secret_store.cpp
    src/app/main.cpp
    src/app/app_ui_bootstrap.cpp
    src/app/app_runtime.cpp
    src/app/native_file_dialogs.cpp
    src/app/recent_projects.cpp
    src/parser/guidelines_parser.cpp
    ${ASSURANCE_FORGE_SHARED_SOURCES}
)

# --- Main application ---
hello_imgui_add_app(assurance-forge ${ASSURANCE_FORGE_APP_SOURCES})
target_include_directories(assurance-forge PRIVATE src ${ASSURANCE_FORGE_IMGUI_INCLUDE_DIRS})
target_link_libraries(assurance-forge PRIVATE imgui pugixml nfd picosha2 nlohmann_json::nlohmann_json ${AF_YAML_CPP_TARGET} CURL::libcurl)
if(WIN32)
    target_link_libraries(assurance-forge PRIVATE bcrypt advapi32 ole32 shell32)
endif()
af_copy_guidelines_data(assurance-forge)

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
    tests/test_sha256.cpp
    tests/test_ai_settings.cpp
    tests/test_ai_service.cpp
    tests/test_ai_claim_review.cpp
    tests/test_openai_provider.cpp
    tests/test_ai_task_runner.cpp
    tests/test_guidelines_parser.cpp
    tests/test_localization.cpp
    tests/test_recent_projects.cpp
    src/ai/ai_service.cpp
    src/ai/ai_claim_review.cpp
    src/ai/ai_settings.cpp
    src/ai/ai_task_runner.cpp
    src/ai/ai_types.cpp
    src/ai/http_client.cpp
    src/ai/openai_provider.cpp
    src/ai/secret_store.cpp
    src/app/recent_projects.cpp
    src/parser/guidelines_parser.cpp
    src/parser/xml_parser.cpp
    src/core/assurance_tree.cpp
    src/core/element_factory.cpp
    src/core/problems/problem_item.cpp
    src/core/problems/problems_manager.cpp
    src/core/project_model.cpp
    src/core/project_service.cpp
    src/core/sha256.cpp
    src/ui/gsn/gsn_layout.cpp
    src/ui/localization.cpp
    src/sacm/sacm_parser.cpp
    src/sacm/sacm_serializer.cpp
)
target_include_directories(tests PRIVATE src ${ASSURANCE_FORGE_IMGUI_INCLUDE_DIRS})
target_link_libraries(tests PRIVATE GTest::gtest_main pugixml imgui picosha2 nlohmann_json::nlohmann_json ${AF_YAML_CPP_TARGET} CURL::libcurl)
if(WIN32)
    target_link_libraries(tests PRIVATE bcrypt advapi32)

    add_custom_command(TARGET tests POST_BUILD
        COMMAND "$<$<BOOL:$<TARGET_RUNTIME_DLLS:tests>>:${CMAKE_COMMAND};-E;copy_if_different;$<TARGET_RUNTIME_DLLS:tests>;$<TARGET_FILE_DIR:tests>>"
        COMMAND_EXPAND_LISTS
    )
endif()
af_copy_guidelines_data(tests)
include(GoogleTest)

gtest_discover_tests(tests
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    DISCOVERY_MODE PRE_TEST
)
add_test(
    NAME GuidelinesDataRuntimeCopy
    COMMAND ${CMAKE_COMMAND} -E compare_files
        "${AF_GUIDELINES_SOURCE_FILE}"
        "$<TARGET_FILE_DIR:tests>/data/guidelines.yaml"
)
