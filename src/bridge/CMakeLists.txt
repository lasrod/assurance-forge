add_library(af_bridge OBJECT
    instance_registry.cpp
    protocol.cpp
    transport.cpp
)
target_link_libraries(af_bridge PUBLIC af_common)
