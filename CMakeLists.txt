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

option(ENABLE_COVERAGE "Enable code coverage instrumentation (Linux/GCC 14+ only)" OFF)
if(ENABLE_COVERAGE)
    if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        message(FATAL_ERROR "ENABLE_COVERAGE requires GCC (got ${CMAKE_CXX_COMPILER_ID}).")
    endif()
    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS 14)
        message(FATAL_ERROR "ENABLE_COVERAGE requires GCC 14 or newer (got ${CMAKE_CXX_COMPILER_VERSION}).")
    endif()
    message(STATUS "Code coverage instrumentation enabled (GCC ${CMAKE_CXX_COMPILER_VERSION})")
    add_compile_options(--coverage -O0 -fcondition-coverage)
    add_link_options(--coverage)
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

set(AF_SCCG_DIST_DIR "${CMAKE_SOURCE_DIR}/external/safety-case-core-guidelines/dist")
set(AF_SCCG_REQUIRED_DIST_FILES
    review_profiles.json
    data_packages.json
)
set(AF_SCCG_OPTIONAL_DIST_FILES
    prechecks.json
)
foreach(AF_SCCG_DIST_FILE IN LISTS AF_SCCG_REQUIRED_DIST_FILES)
    if(NOT EXISTS "${AF_SCCG_DIST_DIR}/${AF_SCCG_DIST_FILE}")
        message(FATAL_ERROR "Missing SCCG distribution file: external/safety-case-core-guidelines/dist/${AF_SCCG_DIST_FILE}\n\nRun:\ngit submodule update --init --recursive\n\nIf the SCCG submodule is checked out but dist is missing, run:\ncd external/safety-case-core-guidelines\npython -m pip install -r requirements.txt\npython scripts/build_dist.py")
    endif()
endforeach()
if(EXISTS "${AF_SCCG_DIST_DIR}/ai_rule_export.jsonl")
    set(AF_SCCG_RULES_JSONL "ai_rule_export.jsonl")
elseif(EXISTS "${AF_SCCG_DIST_DIR}/sccg.rules.jsonl")
    set(AF_SCCG_RULES_JSONL "sccg.rules.jsonl")
else()
    message(FATAL_ERROR "Missing SCCG rules file: external/safety-case-core-guidelines/dist/ must contain ai_rule_export.jsonl or sccg.rules.jsonl\n\nRun:\ngit submodule update --init --recursive\n\nIf the SCCG submodule is checked out but dist is missing, run:\ncd external/safety-case-core-guidelines\npython -m pip install -r requirements.txt\npython scripts/build_dist.py")
endif()

function(af_copy_sccg_data target_name)
    add_custom_command(TARGET ${target_name} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${target_name}>/data/sccg/dist"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${AF_SCCG_REQUIRED_DIST_FILES}
            ${AF_SCCG_RULES_JSONL}
            "$<TARGET_FILE_DIR:${target_name}>/data/sccg/dist"
        WORKING_DIRECTORY "${AF_SCCG_DIST_DIR}"
        VERBATIM
    )
    foreach(AF_SCCG_OPT_FILE IN LISTS AF_SCCG_OPTIONAL_DIST_FILES)
        if(EXISTS "${AF_SCCG_DIST_DIR}/${AF_SCCG_OPT_FILE}")
            add_custom_command(TARGET ${target_name} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${AF_SCCG_DIST_DIR}/${AF_SCCG_OPT_FILE}"
                    "$<TARGET_FILE_DIR:${target_name}>/data/sccg/dist/${AF_SCCG_OPT_FILE}"
                VERBATIM
            )
        endif()
    endforeach()
    if(EXISTS "${AF_SCCG_DIST_DIR}/sccg.full.yaml")
        add_custom_command(TARGET ${target_name} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${target_name}>/data"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${AF_SCCG_DIST_DIR}/sccg.full.yaml"
                "$<TARGET_FILE_DIR:${target_name}>/data/sccg.full.yaml"
            VERBATIM
        )
    endif()
endfunction()

set(ASSURANCE_FORGE_SHARED_SOURCES
    src/export/gsn_svg_exporter.cpp
    src/export/gsn_projection.cpp
    src/export/gsn_svg_layout.cpp
    src/export/svg_writer.cpp
    src/parser/model_utils.cpp
    src/parser/xml_parser.cpp
    src/ui/panels/welcome_modal.cpp
    src/ui/panels/element_panel.cpp
    src/ui/panels/package_details_panel.cpp
    src/ui/panels/preferences_panel.cpp
    src/ui/panels/problems_panel.cpp
    src/ui/panels/project_explorer_panel.cpp
    src/ui/panels/review_panel.cpp
    src/ui/panels/sacm_viewer_panel.cpp
    src/ui/panels/terminology_package_panel.cpp
    src/ui/panels/terminology_usages_panel.cpp
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
    src/core/gsn_layout.cpp
    src/core/string_utils.cpp
    src/core/time_utils.cpp
    src/core/tree_editing.cpp
    src/core/problems/problem_attention.cpp
    src/core/problems/problem_item.cpp
    src/core/problems/problems_manager.cpp
    src/core/problems/problem_utils.cpp
    src/core/project_model.cpp
    src/core/project_service.cpp
    src/core/reviews/review_item.cpp
    src/core/reviews/review_item_manager.cpp
    src/core/reviews/review_proposal.cpp
    src/core/reviews/review_proposal_factory.cpp
    src/core/reviews/review_proposal_manager.cpp
    src/core/reviews/review_proposal_patch_service.cpp
    src/core/reviews/review_text_utils.cpp
    src/core/sha256.cpp
    src/core/terminology_package_service.cpp
    src/core/terminology_scope_service.cpp
    src/core/terminology_text_utils.cpp
    src/sacm/sacm_parser.cpp
    src/sacm/sacm_package_tree.cpp
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
    src/app/controllers/ai_review_controller.cpp
    src/app/app_runtime_ai.cpp
    src/app/app_ui_bootstrap.cpp
    src/app/app_runtime_project.cpp
    src/app/app_runtime.cpp
    src/app/app_runtime_state.cpp
    src/app/actions/ai_review_actions.cpp
    src/app/actions/element_actions.cpp
    src/app/actions/proposal_actions.cpp
    src/app/actions/review_actions.cpp
    src/app/actions/terminology_actions.cpp
    src/app/areas/ai_debug_area.cpp
    src/app/areas/argument_navigator_area.cpp
    src/app/areas/feedback_dock_area.cpp
    src/app/areas/inspector_area.cpp
    src/app/areas/modal_host.cpp
    src/app/areas/problems_area.cpp
    src/app/areas/proposal_editor_area.cpp
    src/app/areas/project_explorer_area.cpp
    src/app/areas/review_panel_area.cpp
    src/app/areas/term_usages_area.cpp
    src/app/areas/workbench_area.cpp
    src/app/frame/app_layout_regions.cpp
    src/app/frame/app_menu_bar.cpp
    src/app/frame/app_shell.cpp
    src/app/guideline_catalog.cpp
    src/app/review_problem_sync.cpp
    src/app/controllers/element_edit_controller.cpp
    src/app/controllers/modal_coordinator.cpp
    src/app/native_file_dialogs.cpp
    src/app/controllers/project_controller.cpp
    src/app/controllers/proposal_controller.cpp
    src/app/project_workflow.cpp
    src/app/proposal_ui_state.cpp
    src/app/recent_projects.cpp
    src/app/controllers/review_controller.cpp
    src/app/sacm_argument_sync.cpp
    src/app/terminology_problem_sync.cpp
    src/parser/guidelines_parser.cpp
    src/parser/sccg_dist_parser.cpp
    ${ASSURANCE_FORGE_SHARED_SOURCES}
)

# --- Main application ---
hello_imgui_add_app(assurance-forge ${ASSURANCE_FORGE_APP_SOURCES})
target_include_directories(assurance-forge PRIVATE src ${ASSURANCE_FORGE_IMGUI_INCLUDE_DIRS})
target_link_libraries(assurance-forge PRIVATE imgui pugixml nfd picosha2 nlohmann_json::nlohmann_json ${AF_YAML_CPP_TARGET} CURL::libcurl)
if(WIN32)
    target_link_libraries(assurance-forge PRIVATE bcrypt advapi32 ole32 shell32)
endif()
af_copy_sccg_data(assurance-forge)

# --- Tests ---
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.16.0
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

include(CTest)
add_executable(tests
    tests/test_xml_parser.cpp
    tests/test_assurance_tree.cpp
    tests/test_layout.cpp
    tests/test_problem_attention.cpp
    tests/test_problems_manager.cpp
    tests/test_review_item.cpp
    tests/test_review_proposal.cpp
    tests/test_review_proposal_patch_service.cpp
    tests/test_sacm_roundtrip.cpp
    tests/test_sacm_package_tree.cpp
    tests/test_terminology_actions.cpp
    tests/test_terminology_package_service.cpp
    tests/test_terminology_scope_service.cpp
    tests/test_element_factory.cpp
    tests/test_tree_editing.cpp
    tests/test_app_state.cpp
    tests/test_project_service.cpp
    tests/test_project_controller.cpp
    tests/test_proposal_controller.cpp
    tests/test_sha256.cpp
    tests/test_app_events.cpp
    tests/test_ai_review_controller.cpp
    tests/test_review_problem_sync.cpp
    tests/test_review_controller.cpp
    tests/test_element_edit_controller.cpp
    tests/test_modal_coordinator.cpp
    tests/test_ai_settings.cpp
    tests/test_ai_service.cpp
    tests/test_ai_claim_review.cpp
    tests/test_openai_provider.cpp
    tests/test_ai_task_runner.cpp
    tests/test_guidelines_parser.cpp
    tests/test_guideline_catalog.cpp
    tests/test_localization.cpp
    tests/test_theme.cpp
    tests/test_ui_state.cpp
    tests/test_recent_projects.cpp
    tests/test_shared_utils.cpp
    tests/test_gsn_svg_exporter.cpp
    src/export/gsn_svg_exporter.cpp
    src/export/gsn_projection.cpp
    src/export/gsn_svg_layout.cpp
    src/export/svg_writer.cpp
    src/ai/ai_service.cpp
    src/ai/ai_claim_review.cpp
    src/ai/ai_settings.cpp
    src/ai/ai_task_runner.cpp
    src/ai/ai_types.cpp
    src/ai/libcurl_http_client.cpp
    src/ai/http_client.cpp
    src/ai/openai_provider.cpp
    src/ai/secret_store.cpp
    src/app/app_runtime_state.cpp
    src/app/actions/terminology_actions.cpp
    src/app/controllers/ai_review_controller.cpp
    src/app/controllers/element_edit_controller.cpp
    src/app/controllers/modal_coordinator.cpp
    src/app/controllers/project_controller.cpp
    src/app/project_workflow.cpp
    src/app/controllers/proposal_controller.cpp
    src/app/recent_projects.cpp
    src/app/guideline_catalog.cpp
    src/app/review_problem_sync.cpp
    src/app/controllers/review_controller.cpp
    src/parser/guidelines_parser.cpp
    src/parser/sccg_dist_parser.cpp
    src/parser/model_utils.cpp
    src/parser/xml_parser.cpp
    src/core/assurance_tree.cpp
    src/core/element_factory.cpp
    src/core/gsn_layout.cpp
    src/core/string_utils.cpp
    src/core/time_utils.cpp
    src/core/tree_editing.cpp
    src/core/problems/problem_attention.cpp
    src/core/problems/problem_item.cpp
    src/core/problems/problems_manager.cpp
    src/core/problems/problem_utils.cpp
    src/core/project_model.cpp
    src/core/project_service.cpp
    src/core/app_state.cpp
    src/core/reviews/review_item.cpp
    src/core/reviews/review_item_manager.cpp
    src/core/reviews/review_proposal.cpp
    src/core/reviews/review_proposal_factory.cpp
    src/core/reviews/review_proposal_manager.cpp
    src/core/reviews/review_proposal_patch_service.cpp
    src/core/reviews/review_text_utils.cpp
    src/core/sha256.cpp
    src/core/terminology_package_service.cpp
    src/core/terminology_scope_service.cpp
    src/core/terminology_text_utils.cpp
    src/ui/gsn/gsn_layout.cpp
    src/ui/localization.cpp
    src/ui/theme.cpp
    src/ui/ui_state.cpp
    src/sacm/sacm_parser.cpp
    src/sacm/sacm_package_tree.cpp
    src/sacm/sacm_serializer.cpp
)
target_include_directories(tests PRIVATE src ${ASSURANCE_FORGE_IMGUI_INCLUDE_DIRS})
target_link_libraries(tests PRIVATE GTest::gtest_main hello_imgui pugixml imgui picosha2 nlohmann_json::nlohmann_json ${AF_YAML_CPP_TARGET} CURL::libcurl)
if(WIN32)
    target_link_libraries(tests PRIVATE bcrypt advapi32)

    add_custom_command(TARGET tests POST_BUILD
        COMMAND "$<$<BOOL:$<TARGET_RUNTIME_DLLS:tests>>:${CMAKE_COMMAND};-E;copy_if_different;$<TARGET_RUNTIME_DLLS:tests>;$<TARGET_FILE_DIR:tests>>"
        COMMAND_EXPAND_LISTS
    )
endif()
af_copy_sccg_data(tests)
include(GoogleTest)

gtest_discover_tests(tests
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    DISCOVERY_MODE PRE_TEST
)
add_test(
    NAME SccgDataRuntimeCopy
    COMMAND ${CMAKE_COMMAND} -E compare_files
    "${AF_SCCG_DIST_DIR}/review_profiles.json"
    "$<TARGET_FILE_DIR:tests>/data/sccg/dist/review_profiles.json"
)
