add_library(af_parser OBJECT
    xml_parser.cpp
    guidelines_parser.cpp
    sccg_dist_parser.cpp
    model_utils.cpp
)
# pugixml parses the SACM XML and yaml-cpp reads the SCCG catalog; both are
# used only in this layer's sources, never in a header it hands out.
target_link_libraries(af_parser PUBLIC af_common)
target_link_libraries(af_parser PRIVATE pugixml ${AF_YAML_CPP_TARGET})
