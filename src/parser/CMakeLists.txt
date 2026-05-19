add_library(af_parser OBJECT
    xml_parser.cpp
    guidelines_parser.cpp
    sccg_dist_parser.cpp
    model_utils.cpp
)
target_link_libraries(af_parser PUBLIC af_common)
