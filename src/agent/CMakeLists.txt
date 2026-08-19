add_library(af_agent OBJECT
    change_operations.cpp
    draft_operations.cpp
    placement.cpp
    read_operations.cpp
)
# `review` is on this list because ADR 0013 puts it there: an external client
# must be served the same review method and the same validator as the built-in
# path, and `agent` is how it reaches them. `mcp` still never includes `review`
# directly -- the layer gate enforces that.
target_link_libraries(af_agent PUBLIC af_common af_review)
