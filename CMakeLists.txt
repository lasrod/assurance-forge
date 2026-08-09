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

# --------------------------------------------------------------------
# Warning policy.
#
# Applied per target rather than globally, because a global
# add_compile_options() would also hit hello_imgui, curl, yaml-cpp and the rest
# of the fetched dependencies. Their warnings are not ours to fix, and drowning
# our own in thousands of theirs is how a warning level gets switched back off.
#
# AF_WARNINGS_AS_ERRORS defaults OFF so that a new compiler version emitting a
# new diagnostic does not break a contributor's local build. CI turns it on, so
# the same diagnostic does block a merge.
# --------------------------------------------------------------------
option(AF_WARNINGS_AS_ERRORS "Treat compiler warnings as errors (CI turns this on)" OFF)

function(af_set_warning_level target)
    if(NOT TARGET ${target})
        message(FATAL_ERROR "af_set_warning_level: no such target '${target}'")
    endif()
    if(MSVC)
        # These three are off by default even at /W4, and each is MSVC's
        # equivalent of a GCC/Clang diagnostic that already fails this build:
        #   /w14505, /w15245 -> -Wunused-function      (dead static / anon-namespace fn)
        #   /w15264          -> -Wunused-const-variable (dead constant)
        # Without them a Windows developer builds clean and CI rejects the branch
        # on GCC and Clang, which teaches people that the local build means
        # nothing. Every one of them found real dead code the first time it ran.
        target_compile_options(${target} PRIVATE /W4 /w14505 /w15245 /w15264)
        # std::getenv and std::fopen are standard C++ that MSVC deprecates on its
        # own authority. The suggested replacements (_dupenv_s, fopen_s) are not
        # portable, and this code builds on three toolchains, so the warning has
        # no action behind it. Scoped to first-party targets, and it does not
        # cover STL deprecations such as std::filesystem::u8path -- that one was
        # a real signal and is fixed rather than silenced.
        target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS)
        if(AF_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        # -Wmissing-field-initializers (part of -Wextra) fires on partial
        # aggregate initialization, which this codebase uses deliberately: a
        # callbacks struct is built positionally for the members that have one
        # and the rest are assigned by name immediately below. Listing every
        # member inside the braces would duplicate those assignments to satisfy
        # a warning that has found nothing. GCC and Clang both already exempt
        # `{}` from it, so the diagnostic is about style, not correctness.
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
        if(AF_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()

# --------------------------------------------------------------------
# Sanitizers.
#
# Global, and deliberately so -- the opposite of the warning policy above. A
# warning in hello_imgui is not ours to fix, but an allocation made by
# uninstrumented code and freed by instrumented code is how ASan produces
# results that are wrong in both directions. Sanitizers only tell the truth
# when everything in the process is built the same way.
#
# -fno-sanitize-recover=all matters more than it looks. UBSan's default is to
# print and carry on, which exits zero: the job goes green while reporting
# undefined behaviour. Aborting is what makes this a check rather than a log.
# --------------------------------------------------------------------
set(AF_SANITIZE "" CACHE STRING
    "Comma-separated sanitizers to instrument the whole build with, e.g. address,undefined. Empty disables.")

if(AF_SANITIZE)
    if(MSVC)
        # Refused rather than half-supported. MSVC has no UBSan at all, and its
        # /fsanitize=address needs the optional "C++ AddressSanitizer" Visual
        # Studio component: without it the build compiles and then fails at link
        # with "cannot open file clang_rt.asan_dynamic_runtime_thunk-x86_64.lib".
        # Measured on this toolchain, not assumed.
        #
        # Nothing in CI exercises MSVC here, so shipping the path would be an
        # untested claim that this option works on Windows. It does not, unless
        # that component happens to be installed, and a build that dies at link
        # is a worse answer than one that says why up front.
        message(FATAL_ERROR
            "AF_SANITIZE is not supported with MSVC.\n"
            "  MSVC has no UndefinedBehaviorSanitizer, and its AddressSanitizer needs the\n"
            "  optional 'C++ AddressSanitizer' Visual Studio component to link.\n"
            "  Use GCC 14, which is what CI sanitizes with:\n"
            "    cmake -B build-asan -DCMAKE_C_COMPILER=gcc-14 -DCMAKE_CXX_COMPILER=g++-14 \\\n"
            "          -DCMAKE_BUILD_TYPE=Debug -DAF_SANITIZE=address,undefined\n"
            "  Clang works too, but must be new enough for std::expected: 18 is not.")
    else()
        message(STATUS "Sanitizers: ${AF_SANITIZE}")
        add_compile_options(
            -fsanitize=${AF_SANITIZE}
            -fno-sanitize-recover=all
            -fno-omit-frame-pointer
            -g
        )
        add_link_options(-fsanitize=${AF_SANITIZE})
    endif()
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

# --- GoogleTest (fetched before libs/sacm so the library reuses it) ---
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.16.0
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# --- SACM 2.3 library (independent; see libs/sacm/README.md) ---
set(SACM_REPO_FIXTURES_DIR ${CMAKE_SOURCE_DIR})
add_subdirectory(libs/sacm)

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

# --- Git hooks ---
# Points core.hooksPath at the checked-in .githooks so staged C/C++ is formatted
# on commit. Configure time, because everyone builds and nobody remembers a
# separate setup step.
# Deliberately NOT named AF_SOURCE_DIR: check_layer_gates.cmake below reads that
# name and defaults it to <repo>/src, so setting it here would silently point the
# layer gate at the repository root instead.
set(AF_REPO_ROOT "${CMAKE_SOURCE_DIR}")
include("${CMAKE_SOURCE_DIR}/cmake/install_git_hooks.cmake")

# --- Layer-gate check ---
# Standalone CMake script enforces src/ include-direction rules.
# Runs at configure time so violations fail fast.
include("${CMAKE_SOURCE_DIR}/cmake/check_layer_gates.cmake")
add_custom_target(check_layer_gates ALL
    COMMAND ${CMAKE_COMMAND}
        -DAF_SOURCE_DIR=${CMAKE_SOURCE_DIR}/src
        -DAF_LIBS_SACM_DIR=${CMAKE_SOURCE_DIR}/libs/sacm
        -P ${CMAKE_SOURCE_DIR}/cmake/check_layer_gates.cmake
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Checking src/ layer gates and libs/sacm independence"
    VERBATIM
)

# --- Main application ---
# Source lists live in src/<layer>/CMakeLists.txt as OBJECT libraries.
# main.cpp is compiled directly into the executable; everything else
# is linked in via the per-layer af_* targets.
hello_imgui_add_app(assurance-forge src/app/main.cpp)
target_link_libraries(assurance-forge PRIVATE
    af_app af_ui af_ai af_export af_agent af_bridge af_core af_sacm_adapter af_legacy_sacm af_parser
)
if(WIN32)
    target_link_libraries(assurance-forge PRIVATE bcrypt advapi32 ole32 shell32 dwmapi)
endif()
af_copy_sccg_data(assurance-forge)

# --- MCP server ---
# A headless companion binary an MCP client launches, speaking JSON-RPC 2.0 over
# stdin/stdout. Deliberately a plain add_executable rather than
# hello_imgui_add_app: it has no window, no event loop, and must not drag in the
# GUI runtime. It links no af_ui and no af_ai — the layer gate forbids `mcp/`
# from including either, which is what keeps the MCP server and the in-app AI
# review from growing a shared inference path (docs/features/mcp-server.md).
add_executable(assurance-forge-mcp src/mcp/main.cpp)
target_link_libraries(assurance-forge-mcp PRIVATE
    af_mcp af_agent af_bridge af_core af_sacm_adapter af_legacy_sacm af_parser
)
if(WIN32)
    target_link_libraries(assurance-forge-mcp PRIVATE bcrypt advapi32)
endif()

# --- Localization catalogs (optional regeneration) ---
# The Japanese .mo is committed and bundled as an asset, so normal builds need
# no gettext tools. This convenience target regenerates the .mo from the .po
# using msgfmt when available, otherwise the dependency-free
# tools/i18n/compile_po.py. It is not part of ALL.
set(AF_JA_PO "${CMAKE_SOURCE_DIR}/assets/locale/ja/LC_MESSAGES/assurance_forge.po")
set(AF_JA_MO "${CMAKE_SOURCE_DIR}/assets/locale/ja/LC_MESSAGES/assurance_forge.mo")
# Python is used both for the optional catalog compile fallback and for the
# catalog/conformance-matrix CTest gates below, so look for it once up front.
find_package(Python3 COMPONENTS Interpreter QUIET)
find_program(MSGFMT_EXECUTABLE msgfmt)
if(MSGFMT_EXECUTABLE)
    add_custom_target(i18n_compile
        COMMAND ${MSGFMT_EXECUTABLE} "${AF_JA_PO}" -o "${AF_JA_MO}"
        COMMENT "Compiling i18n catalogs with msgfmt"
        VERBATIM
    )
elseif(Python3_Interpreter_FOUND)
    add_custom_target(i18n_compile
        COMMAND ${Python3_EXECUTABLE} "${CMAKE_SOURCE_DIR}/tools/i18n/compile_po.py" "${AF_JA_PO}" "${AF_JA_MO}"
        COMMENT "Compiling i18n catalogs with tools/i18n/compile_po.py"
        VERBATIM
    )
endif()

# --- Tests ---
# GoogleTest is fetched earlier (before libs/sacm).
include(CTest)
add_executable(tests
    tests/test_sacm_library_parallel_load.cpp
    tests/test_sacm_library_edit.cpp
    tests/test_sacm_library_save.cpp
    tests/test_save_from_library.cpp
    tests/test_acp_controller.cpp
    tests/test_xml_parser.cpp
    tests/test_multilingual_round_trip.cpp
    tests/test_argument_cycles.cpp
    tests/test_gsn_wellformedness.cpp
    tests/test_gsn_repair.cpp
    tests/test_gsn_repair_fixture.cpp
    tests/test_register_model.cpp
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
    tests/test_ignored_terminology_store.cpp
    tests/test_translation_review_sync.cpp
    tests/test_element_factory.cpp
    tests/test_tree_editing.cpp
    tests/test_app_state.cpp
    tests/test_project_service.cpp
    tests/test_project_controller.cpp
    tests/test_proposal_controller.cpp
    tests/test_sha256.cpp
    tests/test_app_events.cpp
    tests/test_acp_problem_sync.cpp
    tests/test_terminology_problem_sync.cpp
    tests/test_sacm_argument_sync.cpp
    tests/test_terminology_context_projection.cpp
    tests/test_terminology_commands.cpp
    tests/test_package_commands.cpp
    tests/test_sacm_identity.cpp
    tests/test_project_model.cpp
    tests/test_project_summary.cpp
    tests/test_confidence_problem_sync.cpp
    tests/test_ai_review_controller.cpp
    tests/test_ai_review_actions.cpp
    tests/test_review_problem_sync.cpp
    tests/test_review_controller.cpp
    tests/test_review_external_reload.cpp
    tests/test_register_controller.cpp
    tests/test_register_problem_sync.cpp
    tests/test_element_edit_controller.cpp
    tests/test_modal_coordinator.cpp
    tests/test_ai_settings.cpp
    tests/test_ai_service.cpp
    tests/test_secret_store.cpp
    tests/test_ai_claim_review.cpp
    tests/test_openai_provider.cpp
    tests/test_ai_task_runner.cpp
    tests/test_guidelines_parser.cpp
    tests/test_guideline_catalog.cpp
    tests/test_localization.cpp
    tests/test_theme.cpp
    tests/test_element_type_display.cpp
    tests/test_status_bar.cpp
    tests/test_toolbar.cpp
    tests/test_text_ellipsis.cpp
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
    tests/test_event_replayer.cpp
    tests/test_library_replay_convergence.cpp
    tests/test_library_primary_edit_flip.cpp
    tests/test_strategy_migration.cpp
    tests/test_audit_diff.cpp
    tests/test_event_scope.cpp
    tests/test_history_highlights.cpp
    tests/test_history_reconstruction.cpp
    tests/test_reconcile_audit_store.cpp
    tests/test_audit_recovery.cpp
    tests/test_argument_package_projection.cpp
    tests/test_canvas_history_overlay.cpp
    tests/test_audit_baseline.cpp
    tests/test_audit_user_snapshot.cpp
    tests/test_timeline_model_builder.cpp
    tests/test_undo_command.cpp
    tests/test_dialectic_challenge.cpp
    tests/test_projection_coverage.cpp
    tests/test_bridge_protocol.cpp
    tests/test_bridge_endpoint.cpp
    tests/test_bridge_transport.cpp
    tests/test_agent_request_handler.cpp
    tests/test_agent_bridge_controller.cpp
    tests/test_change_sets.cpp
    tests/test_draft_changes_panel.cpp
    tests/test_draft_workspace.cpp
    tests/test_draft_example_project.cpp
    tests/test_agent_change_canvas.cpp
    tests/test_change_set_acceptance.cpp
    tests/test_sccg_staged_checks.cpp
    tests/test_mcp_jsonrpc.cpp
    tests/test_mcp_server.cpp
    tests/test_mcp_modes.cpp
    tests/test_mcp_client_config.cpp
    tests/test_user_settings_path.cpp
)
# Production sources are pulled in via the per-layer af_* OBJECT libraries
# (see src/<layer>/CMakeLists.txt). The tests target intentionally links
# everything so any production header is reachable from tests.
target_compile_definitions(tests PRIVATE AF_REPO_ROOT="${CMAKE_SOURCE_DIR}")
target_link_libraries(tests PRIVATE
    GTest::gtest_main
    hello_imgui
    af_app af_ui af_ai af_export af_mcp af_agent af_bridge af_core af_sacm_adapter af_legacy_sacm af_parser
)

# The per-layer object libraries are not the whole of the first-party code. The
# executables carry their own translation units -- src/app/main.cpp,
# src/mcp/main.cpp -- and the test sources are first-party code that gets read
# and copied like any other. Warning on the libraries alone would leave the
# ratchet with a hole in exactly the files newcomers open first.
af_set_warning_level(assurance-forge)
af_set_warning_level(assurance-forge-mcp)
af_set_warning_level(tests)
if(WIN32)
    target_link_libraries(tests PRIVATE bcrypt advapi32 dwmapi)

    add_custom_command(TARGET tests POST_BUILD
        COMMAND "$<$<BOOL:$<TARGET_RUNTIME_DLLS:tests>>:${CMAKE_COMMAND};-E;copy_if_different;$<TARGET_RUNTIME_DLLS:tests>;$<TARGET_FILE_DIR:tests>>"
        COMMAND_EXPAND_LISTS
    )
endif()
# The client-config test resolves the MCP server binary beside the test
# executable, so it must exist by the time the tests run. Without this the
# test would silently exercise the not-found branch instead.
add_dependencies(tests assurance-forge-mcp)
af_copy_sccg_data(tests)
include(GoogleTest)

# Registered in two passes so that conformance evidence carries a label the
# ordinary tests do not (issue #292). The split rule is mechanical rather than
# editorial: a test whose name embeds a requirement id IS the evidence for that
# requirement, which `sacm_matrix_check` already enforces in the other
# direction. Nothing here decides what counts as conformance; the test's own
# name does.
#
# The two filters are exact complements in gtest's own filter semantics -- a
# leading `-` means "everything except" -- so every test lands in exactly one
# pass. Union and disjointness hold by construction, not by a list kept in step
# by hand, and `ctest -N` counts the same total before and after.
#
# DISCOVERY_TIMEOUT is raised from the 5s default. Listing takes ~0.4s locally,
# but a loaded Windows runner overran 5s and reported "Process terminated due to
# timeout" -- which reads as a broken build rather than a busy machine.
#
# Each pass sets ONE label, and `app.conformance` is one label rather than two.
# That is deliberate and portable: a multi-value LABELS through
# gtest_discover_tests does not survive consistently. GoogleTest.cmake expands
# ${arg_TEST_PROPERTIES} unquoted into set_tests_properties(), so "app;conformance"
# arrives as two arguments and PROPERTIES silently keeps only LABELS=app --
# while escaping it as "app\;conformance" produced two labels on CMake 4.3 and
# ONE combined label on the CI runners. Version-dependent either way, and the
# failure is invisible: the build succeeds and `ctest -N` counts every test.
#
# `ctest -L` matches labels as a REGULAR EXPRESSION, so one compound label gives
# the same selection with none of that: `-L app` matches `app` and
# `app.conformance`, `-L conformance` matches `app.conformance` and
# `library.conformance`. A direct set_tests_properties() call is not re-expanded
# and can still take a real list; both forms appear in this file for that reason.
gtest_discover_tests(tests
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    DISCOVERY_MODE PRE_TEST
    DISCOVERY_TIMEOUT 60
    TEST_FILTER "*SACM23_*:*GSN3_*"
    PROPERTIES LABELS "app.conformance"
)
gtest_discover_tests(tests
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    DISCOVERY_MODE PRE_TEST
    DISCOVERY_TIMEOUT 60
    TEST_FILTER "-*SACM23_*:*GSN3_*"
    PROPERTIES LABELS "app"
)
add_test(
    NAME SccgDataRuntimeCopy
    COMMAND ${CMAKE_COMMAND} -E compare_files
    "${AF_SCCG_DIST_DIR}/review_profiles.json"
    "$<TARGET_FILE_DIR:tests>/data/sccg/dist/review_profiles.json"
)
# `build`, not `gate`. It compares against a file af_copy_sccg_data() puts in
# the runtime directory in a POST_BUILD step, so unlike the repository gates it
# does need the `tests` target built -- and `ctest -L gate` in a configure-only
# tree would fail on it while the surrounding documentation says that subset
# needs no build.
set_tests_properties(SccgDataRuntimeCopy PROPERTIES LABELS "build")

# --- MCP stdio transport gates ---
# The unit tests cover dispatch; these drive the real process through real pipes,
# which is the only way to catch stdout pollution and newline translation. Both
# modes assert the response line count, so anything writing to stdout that is not
# the protocol fails the build rather than silently breaking every client.
foreach(_mcp_mode granted withheld)
    add_test(
        NAME McpStdioSmoke_${_mcp_mode}
        COMMAND ${CMAKE_COMMAND}
            -DAF_MCP_EXE=$<TARGET_FILE:assurance-forge-mcp>
            -DAF_MCP_PROJECT=${CMAKE_SOURCE_DIR}/tests/data/fixture_roundtrip_core_argument.sacm.xml
            -DAF_MCP_INPUT=${CMAKE_SOURCE_DIR}/tests/data/mcp_smoke_handshake.jsonl
            -DAF_MCP_MODE=${_mcp_mode}
            -DAF_MCP_WORK_DIR=${CMAKE_CURRENT_BINARY_DIR}
            -P ${CMAKE_SOURCE_DIR}/cmake/run_mcp_smoke_test.cmake
    )
    # `contract`: these assert the wire behaviour an external client depends on,
    # driving the real process through real pipes. `slow` because each starts a
    # process, which is the thing worth excluding when iterating.
    set_tests_properties(McpStdioSmoke_${_mcp_mode} PROPERTIES LABELS "app;contract;slow")
endforeach()

# Localization catalog consistency gate: fails if any source msgid is missing
# from the .po, or if the committed .mo is out of sync with the .po. Registered
# as a CTest test so it runs with `ctest` locally and in CI. Skipped only when
# no Python interpreter is available.
if(Python3_Interpreter_FOUND)
    add_test(
        NAME i18n_catalog_check
        COMMAND ${Python3_EXECUTABLE} "${CMAKE_SOURCE_DIR}/tools/i18n/check_catalog.py"
    )

    # SACM conformance matrix gate: fails if a `verified` row is not backed by
    # an ID-bearing test, if a test names a requirement the matrix does not
    # define, or if a row cites a path that no longer exists. The matrix is the
    # canonical source of requirement IDs, so drift there degrades every
    # compliance claim built on it.
    add_test(
        NAME sacm_matrix_check
        COMMAND ${Python3_EXECUTABLE} "${CMAKE_SOURCE_DIR}/tools/sacm/check_conformance_matrix.py"
    )

    # GSN v3 conformance matrix gate: verifies the five-part standard taxonomy,
    # per-operation status vocabulary, test evidence, cited paths, and links from
    # the product capability matrix.
    add_test(
        NAME gsn_matrix_check
        COMMAND ${Python3_EXECUTABLE} "${CMAKE_SOURCE_DIR}/tools/gsn/check_conformance_matrix.py"
    )

    # Capability matrix gate: fails if a `supported` row cites no existing test,
    # if a `planned` row already has tests (the signature of a status reality has
    # overtaken), if a cited path has moved, or if the exported JSON the
    # documentation site consumes is stale. The matrix states what the tool can
    # do for a safety argument, so an overstated row is a correctness problem.
    add_test(
        NAME feature_matrix_check
        COMMAND ${Python3_EXECUTABLE} "${CMAKE_SOURCE_DIR}/tools/features/check_feature_matrix.py"
    )

    # Repository hygiene gate: fails if a captured build log, test output, CTest
    # working directory, generated file or scratch file is tracked by git.
    # .gitignore cannot enforce this on its own -- it is ignored for files
    # already in the index, and silent when someone uses `git add -f`.
    add_test(
        NAME no_committed_artifacts_check
        COMMAND ${Python3_EXECUTABLE} "${CMAKE_SOURCE_DIR}/tools/repo/check_no_committed_artifacts.py"
    )

    # Documentation gate: broken internal links, pages reachable from neither the
    # nav nor any other page, generated documents that do not name their
    # generator, and an architecture page that has stopped describing a subsystem
    # the layer gate enforces. Twenty-nine unreachable pages and four mutually
    # inconsistent architecture documents accumulated without anything noticing.
    add_test(
        NAME documentation_check
        COMMAND ${Python3_EXECUTABLE} "${CMAKE_SOURCE_DIR}/tools/docs/check_documentation.py"
    )

    # Layer-gate self-test. The gate is the only mechanical guard on the
    # architecture and it passes on every clean build -- which is also what a
    # silently broken gate looks like. This feeds it violations it must reject
    # and allowed dependencies it must not, using synthetic fixtures so that
    # proving the gate works does not require leaving a real violation in src/.
    add_test(
        NAME layer_gate_negative_check
        COMMAND ${Python3_EXECUTABLE} "${CMAKE_SOURCE_DIR}/tools/repo/check_layer_gate_detects_violations.py"
    )

    # The verification-record index is generated from each record's front matter.
    # Built by hand it misreports: the one failing record whose filename lacks a
    # `-FAIL` suffix reads as a pass.
    add_test(
        NAME verification_index_check
        COMMAND ${Python3_EXECUTABLE} "${CMAKE_SOURCE_DIR}/tools/docs/generate_verification_index.py" --check
    )

    # Release-evidence gate. The release workflow generates a conformance
    # evidence package (#295); this proves the generator still works against the
    # current checkout -- matrix parseable, spec pins recomputable, frozen docs
    # present -- so the first time it runs for real is not on a release tag.
    add_test(
        NAME evidence_package_check
        COMMAND ${Python3_EXECUTABLE} "${CMAKE_SOURCE_DIR}/tools/sacm/generate_evidence_package.py" --check
    )

    # Agent-definition gate. `.claude/agents/` and `.codex/agents/` are generated
    # from `.agents/agents/`; this fails when one is hand-edited, when an adapter
    # has no definition behind it, and when an agent's authority is stated one way
    # in `writes` and granted another in `tools`.
    #
    # It exists because the drift already happened. Nine roles were hand-written
    # twice with nothing comparing them, and `sacm-conformance-verifier` had
    # diverged in its write-authority clause -- for the one role whose value is
    # that it cannot fix what it judges.
    add_test(
        NAME agent_definition_check
        COMMAND ${Python3_EXECUTABLE} "${CMAKE_SOURCE_DIR}/tools/agents/check_agents.py"
    )

    # CTest label gate. Labels decide what `ctest -L` runs and which tests count
    # as conformance evidence, and a wrong label is silent: the build succeeds,
    # `ctest -N` still counts every test, and only the selection is wrong. This
    # asserts every test carries a label, that no label is a semicolon-joined
    # string (the signature of the CMake list-expansion trap documented at the
    # discovery calls), and that `conformance` means exactly "the name embeds a
    # requirement id" in both directions.
    add_test(
        NAME ctest_label_check
        COMMAND ${Python3_EXECUTABLE} "${CMAKE_SOURCE_DIR}/tools/repo/check_ctest_labels.py"
                --build-dir "${CMAKE_BINARY_DIR}" --config "$<CONFIG>"
    )

    # `gate`: these check the repository rather than the product -- catalogues,
    # matrices, documentation, committed artifacts. They need no build, run in
    # about a second together, and are the set worth running before pushing.
    # Listed by name rather than swept up by a pattern, so a new gate has to say
    # it is one.
    set_tests_properties(
        i18n_catalog_check
        sacm_matrix_check
        gsn_matrix_check
        feature_matrix_check
        no_committed_artifacts_check
        documentation_check
        layer_gate_negative_check
        verification_index_check
        agent_definition_check
        evidence_package_check
        PROPERTIES LABELS "gate"
    )

    # `build`, not `gate`, for the same reason as SccgDataRuntimeCopy: it runs
    # `ctest --show-only`, which triggers PRE_TEST discovery and therefore runs
    # the test executables. In a configure-only tree it would see only the
    # add_test() entries and pass having checked almost nothing, so the script
    # refuses a test list that is missing whole executables.
    set_tests_properties(ctest_label_check PROPERTIES LABELS "build")
else()
    # These are compliance gates, not conveniences. A silently absent gate reads
    # as a passing one, so say so loudly rather than skipping in silence.
    message(WARNING
        "Python3 not found: the i18n_catalog_check, sacm_matrix_check, gsn_matrix_check, "
        "feature_matrix_check, no_committed_artifacts_check, documentation_check and "
        "verification_index_check gates will NOT run. "
        "CI installs Python explicitly; a local build without it cannot verify the "
        "translation catalog, the SACM or GSN conformance matrices, the capability "
        "matrix, that no build artifacts have been committed, that documentation "
        "links resolve and pages are reachable, or that the verification index "
        "matches the records.")
endif()

