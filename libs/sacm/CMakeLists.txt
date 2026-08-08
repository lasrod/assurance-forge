# --------------------------------------------------------------------
# sacm — independent, editable OMG SACM 2.3 library (C++23).
#
# This library owns SACM model state, SACM-native editing with previews,
# XMI import/export, validation, identity, and semantic comparison. It
# must stay free of Assurance Forge dependencies: no src/** includes, no
# ImGui/UI/app/AI types, strict SACM terminology only (enforced by
# cmake/check_layer_gates.cmake in the parent build).
#
# Builds in-tree (add_subdirectory from the Assurance Forge root) and
# standalone (cmake -S libs/sacm -B <build>).
# --------------------------------------------------------------------
cmake_minimum_required(VERSION 3.21)
project(sacm VERSION 0.1.0 LANGUAGES CXX)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(SACM_STANDALONE ON)
else()
    set(SACM_STANDALONE OFF)
endif()

if(SACM_STANDALONE)
    set(CMAKE_CXX_STANDARD 23)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    if(MSVC)
        add_compile_options(/FS /utf-8 /MP)
    endif()
endif()

add_library(sacm STATIC
    src/version.cpp
    src/metadata/element_kind.cpp
    src/metadata/namespaces.cpp
    src/model/argumentation.cpp
    src/model/artifact.cpp
    src/model/document.cpp
    src/model/element.cpp
    src/model/lang_string.cpp
    src/model/traverse.cpp
    src/commands/commands.cpp
    src/commands/mutation.cpp
    src/commands/operations.cpp
    src/compare/semantic_compare.cpp
    src/compat/preserve.cpp
    src/io/name_tables.cpp
    src/io/xmi_reader.cpp
    src/io/xmi_writer.cpp
    src/validation/diagnostic.cpp
    src/validation/validate.cpp
)
add_library(sacm::sacm ALIAS sacm)
target_compile_features(sacm PUBLIC cxx_std_23)
target_include_directories(sacm PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
# Internal headers (model/access.h, model/traverse.h, commands/commands.h).
target_include_directories(sacm PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(sacm PUBLIC SACM_STANDARD_VERSION="2.3")

# pugixml: reuse the in-tree target when the parent provides it, else the
# sibling submodule, else FetchContent (fully standalone checkout).
if(NOT TARGET pugixml)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../external/pugixml/CMakeLists.txt")
        add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../external/pugixml" pugixml EXCLUDE_FROM_ALL)
    else()
        include(FetchContent)
        FetchContent_Declare(
            pugixml
            URL https://github.com/zeux/pugixml/releases/download/v1.14/pugixml-1.14.tar.gz
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        )
        FetchContent_MakeAvailable(pugixml)
    endif()
endif()
# PRIVATE: pugixml must never appear in public sacm headers.
target_link_libraries(sacm PRIVATE pugixml)

# The library builds standalone, so it cannot rely on the parent project having
# defined the warning policy.
if(COMMAND af_set_warning_level)
    af_set_warning_level(sacm)
elseif(MSVC)
    target_compile_options(sacm PRIVATE /W4)
else()
    target_compile_options(sacm PRIVATE -Wall -Wextra)
endif()

if(MSVC)
    # C4456: `else if (auto* pkg = dynamic_cast<...>)` chains in the XMI reader
    # and writer, where each branch reuses the same name and MSVC counts the
    # previous branch's variable as still in scope. Sixteen instances, all in
    # that one pattern, none of them a live shadowing bug.
    #
    # Suppressed rather than renamed because renaming means editing
    # libs/sacm/src, which carries a conformance obligation -- a matrix update or
    # a verification record -- that does not belong in a change about warning
    # levels. Cleaning up the pattern is worth doing on its own terms.
    target_compile_options(sacm PRIVATE /wd4456)
endif()

add_executable(sacm_cli tools/sacm_cli.cpp)
target_link_libraries(sacm_cli PRIVATE sacm::sacm)
if(COMMAND af_set_warning_level)
    af_set_warning_level(sacm_cli)
endif()

option(SACM_BUILD_TESTS "Build SACM library tests" ON)
if(SACM_BUILD_TESTS)
    if(NOT TARGET GTest::gtest_main)
        include(FetchContent)
        set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
        FetchContent_Declare(
            googletest
            GIT_REPOSITORY https://github.com/google/googletest.git
            GIT_TAG v1.16.0
        )
        FetchContent_MakeAvailable(googletest)
    endif()
    enable_testing()
    add_executable(sacm_tests
        tests/test_version.cpp
        tests/test_document_commands.cpp
        tests/test_delete_preview.cpp
        tests/test_xmi_io.cpp
        tests/test_roundtrip.cpp
        tests/test_attribute_preservation.cpp
        tests/test_base_model.cpp
        tests/test_repo_fixture_interop.cpp
        tests/test_interop_corpus.cpp
        tests/test_packages.cpp
        tests/test_terminology.cpp
        tests/test_argumentation.cpp
        tests/test_artifact.cpp
        tests/test_metamodel_coverage.cpp
        tests/test_compliance_points.cpp
    )
    target_link_libraries(sacm_tests PRIVATE sacm::sacm GTest::gtest_main)
    if(COMMAND af_set_warning_level)
        af_set_warning_level(sacm_tests)
    endif()
    target_compile_definitions(sacm_tests PRIVATE
        SACM_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/data"
    )
    # Tests live inside the library and may assert on internal tables, e.g. the
    # losslessness attribute table in src/io/name_tables.h.
    target_include_directories(sacm_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    # pugixml stays PRIVATE to `sacm` so it never reaches a public header, but
    # the preservation test needs it deliberately: it reads the source and the
    # saved XML through a parser that is not the library's, so an attribute the
    # library's own reader drops has nowhere to hide. Linking it to the test
    # target does not put it in the public surface.
    target_link_libraries(sacm_tests PRIVATE pugixml)
    if(DEFINED SACM_REPO_FIXTURES_DIR)
        target_compile_definitions(sacm_tests PRIVATE
            SACM_REPO_FIXTURES_DIR="${SACM_REPO_FIXTURES_DIR}"
        )
    endif()
    include(GoogleTest)
    # Two passes, so conformance evidence is selectable apart from the rest.
    # The filters are exact complements in gtest's own semantics, so every test
    # lands in exactly one. See the longer note at the `tests` registration in
    # the top-level CMakeLists (issue #292).
    gtest_discover_tests(sacm_tests
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        DISCOVERY_MODE PRE_TEST
        DISCOVERY_TIMEOUT 60
        TEST_FILTER "*SACM23_*:*GSN3_*"
        PROPERTIES LABELS "library.conformance"
    )
    gtest_discover_tests(sacm_tests
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        DISCOVERY_MODE PRE_TEST
        DISCOVERY_TIMEOUT 60
        TEST_FILTER "-*SACM23_*:*GSN3_*"
        PROPERTIES LABELS "library"
    )
    # CLI smoke workflows (SACM23-CLI-001). These cover the round-trip and
    # validate paths that were previously only exercised by hand, so the
    # requirement rests on CI rather than on someone remembering to run it.
    set(SACM_CLI_FIXTURES "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/sacm23")
    add_test(NAME SacmCliVersionSmoke COMMAND sacm_cli version)
    add_test(NAME SacmCliRoundtripStrict
             COMMAND sacm_cli roundtrip "${SACM_CLI_FIXTURES}/package-minimal-valid.sacm.xmi"
                     --strict)
    # Two tests, because CTest cannot assert both cleanly in one: setting
    # PASS_REGULAR_EXPRESSION makes the regex decide pass/fail and ignores the
    # exit code, so combining it with WILL_FAIL inverts the result backwards.
    #
    # (a) invalid input exits non-zero -- a CLI that reports success on a broken
    #     file is worse than no CLI.
    add_test(NAME SacmCliValidateRejectsDuplicateIds
             COMMAND sacm_cli validate "${SACM_CLI_FIXTURES}/invalid/ids-duplicate-invalid.sacm.xmi")
    set_tests_properties(SacmCliValidateRejectsDuplicateIds PROPERTIES WILL_FAIL TRUE)
    # (b) it fails for the *right reason*. Without this, the test above passes
    #     just as happily on a misspelled fixture path, which is the defect it
    #     is supposed to rule out.
    add_test(NAME SacmCliValidateReportsDuplicateIdDiagnostic
             COMMAND sacm_cli validate "${SACM_CLI_FIXTURES}/invalid/ids-duplicate-invalid.sacm.xmi")
    set_tests_properties(SacmCliValidateReportsDuplicateIdDiagnostic PROPERTIES
        PASS_REGULAR_EXPRESSION "SACM-ID-001")

    # `cli` because they drive the real executable rather than the library API,
    # and `conformance` because SACM23-CLI-001 rests on them -- the same rule
    # the gtest split uses, applied by hand here because these names carry the
    # requirement in the matrix rather than in the test name.
    set_tests_properties(
        SacmCliVersionSmoke
        SacmCliRoundtripStrict
        SacmCliValidateRejectsDuplicateIds
        SacmCliValidateReportsDuplicateIdDiagnostic
        PROPERTIES LABELS "library;cli;conformance"
    )
endif()
