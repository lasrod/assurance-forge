add_library(af_agent OBJECT
    change_operations.cpp
    placement.cpp
    read_operations.cpp
)
target_link_libraries(af_agent PUBLIC af_common)
