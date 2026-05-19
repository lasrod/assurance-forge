add_library(af_export OBJECT
    gsn_projection.cpp
    gsn_svg_exporter.cpp
    gsn_svg_layout.cpp
    svg_writer.cpp
)
target_link_libraries(af_export PUBLIC af_common)
