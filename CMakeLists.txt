cmake_minimum_required(VERSION 3.21)
project(assurance-forge LANGUAGES C CXX)

if(APPLE)
    enable_language(OBJC OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    # /MP enables per-project parallel compilation across translation units.
    add_compile_options(/FS /utf-8 /MP)
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

# hello_imgui sets CMAKE_CXX_STANDARD to 17 in its own top-level CMake,
# which affects subsequently-created targets in this project. Reassert
# our required language level for Assurance Forge targets.
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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

add_subdirectory(src)

# --- Layer-gate check ---
# Standalone CMake script enforces src/ include-direction rules.
# Runs at configure time so violations fail fast.
include(${CMAKE_SOURCE_DIR}/cmake/check_layer_gates.cmake)
add_custom_target(check_layer_gates ALL
    COMMAND ${CMAKE_COMMAND}
        -DAF_SOURCE_DIR=${CMAKE_SOURCE_DIR}/src
        -P ${CMAKE_SOURCE_DIR}/cmake/check_layer_gates.cmake
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Checking src/ layer gates"
    VERBATIM
)

# --- Main application ---
# Source lists live in src/<layer>/CMakeLists.txt as OBJECT libraries.
# main.cpp is compiled directly into the executable; everything else
# is linked in via the per-layer af_* targets.
hello_imgui_add_app(assurance-forge src/app/main.cpp)
target_link_libraries(assurance-forge PRIVATE
    af_app af_ui af_ai af_export af_core af_sacm af_parser
)
if(WIN32)
    target_link_libraries(assurance-forge PRIVATE bcrypt advapi32 ole32 shell32 dwmapi)
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
    tests/test_terminology_visible_term_context.cpp
    tests/test_element_factory.cpp
    tests/test_tree_editing.cpp
    tests/test_app_state.cpp
    tests/test_project_service.cpp
    tests/test_project_controller.cpp
    tests/test_proposal_controller.cpp
    tests/test_sha256.cpp
    tests/test_app_events.cpp
    tests/test_acp_problem_sync.cpp
    tests/test_confidence_problem_sync.cpp
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
    tests/test_confidence_model.cpp
    tests/test_confidence_store.cpp
    tests/test_assurance_claim_point.cpp
    tests/test_acp_editing.cpp
    tests/test_acp_relationship_index.cpp
    tests/test_recent_projects.cpp
    tests/test_shared_utils.cpp
    tests/test_gsn_svg_exporter.cpp
    tests/test_canonical_model_hash.cpp
    tests/test_audit_manifest.cpp
    tests/test_event_store_jsonl.cpp
    tests/test_initial_snapshot.cpp
    tests/test_command_bus.cpp
)
# Production sources are pulled in via the per-layer af_* OBJECT libraries
# (see src/<layer>/CMakeLists.txt). The tests target intentionally links
# everything so any production header is reachable from tests.
target_link_libraries(tests PRIVATE
    GTest::gtest_main
    hello_imgui
    af_app af_ui af_ai af_export af_core af_sacm af_parser
)
if(WIN32)
    target_link_libraries(tests PRIVATE bcrypt advapi32 dwmapi)

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
