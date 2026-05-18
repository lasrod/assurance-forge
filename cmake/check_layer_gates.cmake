# --------------------------------------------------------------------
# check_layer_gates.cmake — enforce src/ layer boundaries.
#
# Usage (standalone):
#   cmake -DAF_SOURCE_DIR=<repo>/src -P cmake/check_layer_gates.cmake
#
# Layering (a layer may include itself and any layer to its left):
#
#   [model]    core/sacm_model.h, core/element_factory.h (POD types)
#   [parser]   parser/*           → may include model
#   [sacm]     sacm/*             → may include model, parser
#   [core]     core/*             → may include model, parser, sacm
#   [ai]       ai/*               → may include model, parser, sacm, core
#   [export]   export/*           → may include model, parser, sacm, core
#   [ui]       ui/*               → may include model, parser, sacm, core
#   [app]      app/*              → may include everything
#
# Note: parser → core dep is intentional because core/sacm_model.h owns
# the POD schema and re-exports parser:: aliases (Phase 2 migration).
#
# Cross-layer violations beyond ALLOWLIST below cause a FATAL_ERROR.
# --------------------------------------------------------------------

if(NOT DEFINED AF_SOURCE_DIR)
    set(AF_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../src")
endif()
get_filename_component(AF_SOURCE_DIR "${AF_SOURCE_DIR}" ABSOLUTE)

# Forbidden include prefixes per layer. Format: LAYER -> list of forbidden prefixes.
set(_AF_FORBIDDEN_parser "sacm/;ai/;export/;ui/;app/")
set(_AF_FORBIDDEN_sacm   "ai/;export/;ui/;app/")
set(_AF_FORBIDDEN_core   "ai/;export/;ui/;app/")
set(_AF_FORBIDDEN_ai     "export/;ui/;app/")
set(_AF_FORBIDDEN_export "ai/;ui/;app/")
set(_AF_FORBIDDEN_ui     "ai/;export/;app/")
# app may include anything.

# Known cross-layer includes recorded as exceptions. Format:
#   <layer>:<relative-path-from-src>=<allowed-include-prefix>
# Multiple allowed prefixes for the same file can be listed as separate entries.
set(_AF_ALLOWLIST
    "ui:ui/panels/preferences_panel.h=ai/"
    "ui:ui/panels/welcome_modal.h=app/"
)

set(_AF_LAYERS parser sacm core ai export ui)
set(_AF_VIOLATIONS "")

foreach(layer IN LISTS _AF_LAYERS)
    set(layer_dir "${AF_SOURCE_DIR}/${layer}")
    if(NOT IS_DIRECTORY "${layer_dir}")
        continue()
    endif()
    file(GLOB_RECURSE sources
        "${layer_dir}/*.h"
        "${layer_dir}/*.hpp"
        "${layer_dir}/*.cpp"
        "${layer_dir}/*.cc"
    )
    set(forbidden "${_AF_FORBIDDEN_${layer}}")
    foreach(source IN LISTS sources)
        file(RELATIVE_PATH rel "${AF_SOURCE_DIR}" "${source}")
        # Read include lines only.
        file(STRINGS "${source}" include_lines REGEX "^[ \t]*#[ \t]*include[ \t]+\"[a-zA-Z0-9_/]+/[a-zA-Z0-9_.]+\"")
        foreach(line IN LISTS include_lines)
            if(line MATCHES "include[ \t]+\"([a-zA-Z0-9_/.]+)\"")
                set(header "${CMAKE_MATCH_1}")
                foreach(bad IN LISTS forbidden)
                    string(LENGTH "${bad}" bad_len)
                    string(SUBSTRING "${header}" 0 ${bad_len} prefix)
                    if(prefix STREQUAL bad)
                        # Check allow-list.
                        set(key "${layer}:${rel}=${bad}")
                        # Normalize backslashes (Windows file() may use them).
                        string(REPLACE "\\" "/" key "${key}")
                        list(FIND _AF_ALLOWLIST "${key}" idx)
                        if(idx EQUAL -1)
                            list(APPEND _AF_VIOLATIONS "${rel} includes \"${header}\" (layer '${layer}' forbids '${bad}')")
                        endif()
                    endif()
                endforeach()
            endif()
        endforeach()
    endforeach()
endforeach()

list(LENGTH _AF_VIOLATIONS n)
if(n GREATER 0)
    message(STATUS "Layer-gate violations (${n}):")
    foreach(v IN LISTS _AF_VIOLATIONS)
        message(STATUS "  ${v}")
    endforeach()
    message(FATAL_ERROR "Layer-gate check failed: ${n} forbidden include(s). Move the code or add an explicit allow-list entry in cmake/check_layer_gates.cmake.")
endif()

message(STATUS "Layer-gate check passed (${AF_SOURCE_DIR}).")
