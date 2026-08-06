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
#   [bridge]   bridge/*           → may include model, parser, sacm, core
#   [agent]    agent/*            → may include model, parser, sacm, core, bridge
#   [mcp]      mcp/*              → may include model, parser, sacm, core, bridge, agent
#   [app]      app/*              → may include everything
#
# `mcp` forbidding `ai/` is deliberate, not incidental: the MCP server and the
# in-app AI review are two independent features that must not share an inference
# path (docs/features/mcp-server.md). The gate is what keeps that separation from
# eroding by convenience.
#
# `bridge` is the local IPC boundary between the MCP adapter and the running
# application (docs/architecture/decisions/0008-*). It sits below both so the
# two can share a wire contract without either depending on the other, and it
# forbids `core`-and-above vocabulary leaking downward the same way every other
# layer does.
#
# `agent` holds the operations an AI agent performs on a safety case. It is
# separate from `mcp` because the application executes the same operations when
# a client is connected live, and separate from `bridge` because a transport
# that knows what a claim is becomes a second model of the argument. Both the
# adapter and the application link it, which is what stops online and offline
# behaviour from drifting apart.
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
set(_AF_FORBIDDEN_sacm_adapter "ai/;export/;ui/;app/")
set(_AF_FORBIDDEN_core   "ai/;export/;ui/;app/")
set(_AF_FORBIDDEN_ai     "export/;ui/;app/")
set(_AF_FORBIDDEN_export "ai/;ui/;app/")
set(_AF_FORBIDDEN_ui     "ai/;export/;app/")
set(_AF_FORBIDDEN_bridge "ai/;export/;ui/;agent/;mcp/;app/")
set(_AF_FORBIDDEN_agent  "ai/;export/;ui/;mcp/;app/")
set(_AF_FORBIDDEN_mcp    "ai/;export/;ui/;app/")
# app may include anything.

# Known cross-layer includes recorded as exceptions. Format:
#   <layer>:<relative-path-from-src>=<allowed-include-prefix>
# Multiple allowed prefixes for the same file can be listed as separate entries.
# Empty, and worth keeping that way. The two entries that used to live here --
# preferences_panel.h reaching into `ai/` and welcome_modal.h into `app/` -- were
# removed by giving each panel its own view type and mapping onto it in `app`,
# not by rewording the rule. An allow-list is where a dependency rule goes to
# die: each entry is individually reasonable and collectively they mean the gate
# no longer describes the architecture.
#
# Before adding one, try the three things that removed the last two: invert the
# dependency, extract an interface, or relocate the type. If an entry is truly
# unavoidable it needs an ADR and an issue to remove it.
set(_AF_ALLOWLIST
)

set(_AF_LAYERS parser sacm sacm_adapter core ai export ui bridge agent mcp)
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

# --------------------------------------------------------------------
# libs/sacm independence gate (SACM23-LIB-001).
#
# The SACM library must never include Assurance Forge headers or the
# app's third-party UI stack. Scans quoted AND angle-bracket includes.
# pugixml is a private implementation detail: allowed in src/tests/tools,
# forbidden in public headers under include/.
# --------------------------------------------------------------------
if(NOT DEFINED AF_LIBS_SACM_DIR)
    set(AF_LIBS_SACM_DIR "${CMAKE_CURRENT_LIST_DIR}/../libs/sacm")
endif()
get_filename_component(AF_LIBS_SACM_DIR "${AF_LIBS_SACM_DIR}" ABSOLUTE)

if(IS_DIRECTORY "${AF_LIBS_SACM_DIR}")
    # Assurance Forge layer prefixes plus legacy app SACM headers (sacm/sacm_*;
    # the library's own headers use sacm/<area>/... and sacm/version.h).
    set(_SACM_FORBIDDEN_PREFIXES
        "app/" "ui/" "ai/" "core/" "export/" "parser/" "sacm/sacm_"
    )
    # App third-party surface the library must not touch.
    set(_SACM_FORBIDDEN_THIRDPARTY
        "hello_imgui" "imgui" "nlohmann" "yaml-cpp" "curl/" "nfd" "picosha2"
    )
    file(GLOB_RECURSE _sacm_sources
        "${AF_LIBS_SACM_DIR}/include/*.h"
        "${AF_LIBS_SACM_DIR}/include/*.hpp"
        "${AF_LIBS_SACM_DIR}/src/*.h"
        "${AF_LIBS_SACM_DIR}/src/*.hpp"
        "${AF_LIBS_SACM_DIR}/src/*.cpp"
        "${AF_LIBS_SACM_DIR}/src/*.cc"
        "${AF_LIBS_SACM_DIR}/tests/*.cpp"
        "${AF_LIBS_SACM_DIR}/tests/*.h"
        "${AF_LIBS_SACM_DIR}/tools/*.cpp"
    )
    foreach(source IN LISTS _sacm_sources)
        file(RELATIVE_PATH rel "${AF_LIBS_SACM_DIR}" "${source}")
        string(REPLACE "\\" "/" rel "${rel}")
        file(STRINGS "${source}" include_lines REGEX "^[ \t]*#[ \t]*include[ \t]+[\"<]")
        foreach(line IN LISTS include_lines)
            if(line MATCHES "include[ \t]+[\"<]([a-zA-Z0-9_/.+-]+)[\">]")
                set(header "${CMAKE_MATCH_1}")
                foreach(bad IN LISTS _SACM_FORBIDDEN_PREFIXES _SACM_FORBIDDEN_THIRDPARTY)
                    string(LENGTH "${bad}" bad_len)
                    string(SUBSTRING "${header}" 0 ${bad_len} prefix)
                    if(prefix STREQUAL bad)
                        list(APPEND _AF_VIOLATIONS "libs/sacm/${rel} includes \"${header}\" (SACM library independence)")
                    endif()
                endforeach()
                if(rel MATCHES "^include/" AND header MATCHES "^pugixml")
                    list(APPEND _AF_VIOLATIONS "libs/sacm/${rel} includes \"${header}\" (pugixml must not leak into public SACM headers)")
                endif()
            endif()
        endforeach()
    endforeach()
endif()

list(LENGTH _AF_VIOLATIONS n)
if(n GREATER 0)
    message(STATUS "Layer-gate violations (${n}):")
    foreach(v IN LISTS _AF_VIOLATIONS)
        message(STATUS "  ${v}")
    endforeach()
    message(FATAL_ERROR "Layer-gate check failed: ${n} forbidden include(s). Move the code or add an explicit allow-list entry in cmake/check_layer_gates.cmake.")
endif()

message(STATUS "Layer-gate check passed (${AF_SOURCE_DIR}; libs/sacm independence).")
