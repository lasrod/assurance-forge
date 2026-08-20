add_library(af_review OBJECT
    sccg/sccg_review.cpp
    sccg/sccg_profile_selector.cpp
    sccg/suggestion_mapping.cpp
    sccg/sccg_prechecks.cpp
)
# The review layer owns review methods: what to review, which SCCG profile
# applies, what data is packaged, how the request is worded, and how a result
# is validated. It never talks to a provider — the layer gate forbids
# `review/` from including `ai/` and `ai/` from including `review/`
# (ADR 0013), so the method stays reusable by the app and by `agent` without
# ever sharing an inference path or a credential.
target_link_libraries(af_review PUBLIC af_common)
