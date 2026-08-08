add_library(af_sacm OBJECT
    sacm_parser.cpp
    sacm_package_tree.cpp
    sacm_serializer.cpp
)
# The legacy SACM reader and writer. pugixml is an implementation detail of
# the three sources that use it.
target_link_libraries(af_sacm PUBLIC af_common)
target_link_libraries(af_sacm PRIVATE pugixml)
