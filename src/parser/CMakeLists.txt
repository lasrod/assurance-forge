add_library(af_parser OBJECT
    xml_parser.cpp
    guidelines_parser.cpp
    sccg_dist_parser.cpp
    model_utils.cpp
)
# pugixml parses the SACM XML, used only in this layer's sources, never in a
# header it hands out. The SCCG catalogue is JSON (nlohmann, via af_common).
target_link_libraries(af_parser PUBLIC af_common)
target_link_libraries(af_parser PRIVATE pugixml)
