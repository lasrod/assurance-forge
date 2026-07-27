add_library(af_agent OBJECT
    read_operations.cpp
)
target_link_libraries(af_agent PUBLIC af_common)
