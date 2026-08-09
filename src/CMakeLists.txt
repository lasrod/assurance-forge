# --------------------------------------------------------------------
# Assurance-Forge source tree
#
# Each top-level src/ subdirectory is a thin CMake OBJECT library:
#   af_parser, af_legacy_sacm, af_sacm_adapter, af_core, af_ai, af_export, af_ui,
#   af_bridge, af_agent, af_mcp, af_app
#
# Each declares the third-party libraries it actually uses. The map is in
# docs/architecture/layers-and-ownership.md; `af_sacm_adapter` was the pattern
# the rest were brought up to.
#
# `af_common` used to carry the whole third-party surface -- imgui, pugixml,
# nfd, picosha2, nlohmann_json, yaml-cpp and libcurl -- on all eleven targets.
# #301 measured what that meant: `export` and `sacm_adapter` used none of the
# seven, `core` used two, `ui` used one. A target that links what it does not
# use cannot say what it depends on, and neither can anyone reading it.
# --------------------------------------------------------------------

# The include root, so a cross-layer include reads as "core/app_state.h", plus
# the one library too widely reachable to attribute usefully: nlohmann_json is
# in the public headers of core, bridge, agent and mcp, so most of the tree
# meets it by inclusion rather than by convenience. Header-only, so this is an
# include path rather than a link.
#
# It is not a place to put the next dependency. Adding one here gives it to
# eleven targets to spare one of them a line.
add_library(af_common INTERFACE)
target_include_directories(af_common INTERFACE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(af_common INTERFACE nlohmann_json::nlohmann_json)

# Dear ImGui's headers, needed by the two layers that draw. Attached to those
# targets rather than to af_common, which is what put `imgui.h` on the include
# path of the SACM parser.
add_library(af_imgui INTERFACE)
target_include_directories(af_imgui INTERFACE ${ASSURANCE_FORGE_IMGUI_INCLUDE_DIRS})
target_link_libraries(af_imgui INTERFACE imgui)

add_subdirectory(parser)
add_subdirectory(legacy_sacm)
add_subdirectory(sacm_adapter)
add_subdirectory(core)
add_subdirectory(ai)
add_subdirectory(export)
add_subdirectory(ui)
add_subdirectory(bridge)
add_subdirectory(agent)
add_subdirectory(mcp)
add_subdirectory(app)

# Listed explicitly rather than globbed: a new subsystem should have to say that
# it builds warning-clean, not inherit the claim by being in the right folder.
foreach(af_target
    af_parser af_legacy_sacm af_sacm_adapter af_core af_ai af_export af_ui
    af_bridge af_agent af_mcp af_app)
    af_set_warning_level(${af_target})
endforeach()
