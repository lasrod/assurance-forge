add_library(af_sacm OBJECT
    sacm_parser.cpp
    sacm_package_tree.cpp
    sacm_serializer.cpp
)
target_link_libraries(af_sacm PUBLIC af_common)
