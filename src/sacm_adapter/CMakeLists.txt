# --------------------------------------------------------------------
# af_sacm_adapter — the seam between Assurance Forge and the independent
# SACM 2.3 library (libs/sacm).
#
# This is the only Assurance Forge target that links sacm::sacm. Everything
# above it sees LibraryDocument as an opaque handle, so no `sacm::model`
# name reaches core/, ui/, or app/ — which also keeps the legacy
# `namespace sacm` (src/sacm/) unambiguous.
# --------------------------------------------------------------------
add_library(af_sacm_adapter OBJECT
    library_load.cpp
    case_projection.cpp
    projection_diff.cpp
)

target_include_directories(af_sacm_adapter PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(af_sacm_adapter PUBLIC sacm::sacm)
target_compile_features(af_sacm_adapter PUBLIC cxx_std_23)
