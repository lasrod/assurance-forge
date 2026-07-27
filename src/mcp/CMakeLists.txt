add_library(af_mcp OBJECT
    guidance.cpp
    jsonrpc.cpp
    server.cpp
    session.cpp
    tools.cpp
)
target_link_libraries(af_mcp PUBLIC af_common)
