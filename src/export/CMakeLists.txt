add_library(af_export OBJECT
    gsn_projection.cpp
    gsn_svg_exporter.cpp
    gsn_svg_layout.cpp
    svg_writer.cpp
)
# The SVG exporter writes its own markup and needs nothing third-party. It
# linked all seven libraries before #291 and used none of them.
target_link_libraries(af_export PUBLIC af_common)
