# --------------------------------------------------------------------
# Assurance-Forge source tree
#
# Each top-level src/ subdirectory is a thin CMake OBJECT library:
#   af_parser, af_sacm, af_sacm_adapter, af_core, af_ai, af_export, af_ui,
#   af_mcp, af_app
#
# All libraries depend on `af_common` (INTERFACE) which carries the
# include dirs and the third-party link surface. This keeps per-layer
# CMakeLists.txt small (Godot-style) without requiring a granular
# per-layer dependency audit. Layer-gate tightening can be performed
# incrementally later.
# --------------------------------------------------------------------

add_library(af_common INTERFACE)
target_include_directories(af_common INTERFACE
    ${CMAKE_SOURCE_DIR}/src
    ${ASSURANCE_FORGE_IMGUI_INCLUDE_DIRS}
)
target_link_libraries(af_common INTERFACE
    imgui
    pugixml
    nfd
    picosha2
    nlohmann_json::nlohmann_json
    ${AF_YAML_CPP_TARGET}
    CURL::libcurl
)

add_subdirectory(parser)
add_subdirectory(sacm)
add_subdirectory(sacm_adapter)
add_subdirectory(core)
add_subdirectory(ai)
add_subdirectory(export)
add_subdirectory(ui)
add_subdirectory(mcp)
add_subdirectory(app)
