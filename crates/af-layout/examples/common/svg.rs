//! The render model as a plain SVG, for looking at a layout outside the
//! application. Shared by the `preview` and `gallery` examples.
//!
//! This is not the SVG export, which is a later step and will be written from
//! the same render model with the faces embedded. It draws only enough to
//! judge the geometry. With `faces` it loads the measured faces from that
//! folder (relative to the SVG), so its text matches the layout's measurement;
//! without, it names IBM Plex and matches only where those faces are installed.

use std::collections::HashSet;
use std::fmt::Write as _;

use af_layout::{Arrowhead, Decorator, Diagram, LineStyle, Report, ShapeKind, TextRole, Weight};

/// The measured faces' files, as `faces` names them.
pub const FACE_FILES: [&str; 5] = [
    "IBMPlexSans-Regular.ttf",
    "IBMPlexSans-SemiBold.ttf",
    "IBMPlexSansJP-Regular.ttf",
    "IBMPlexSansJP-SemiBold.ttf",
    "IBMPlexMono-Regular.ttf",
];

fn font_faces(folder: &str) -> String {
    let face = |family: &str, file: &str, weight: u16| {
        format!(
            "@font-face{{font-family:'{family}';src:url('{folder}/{file}');font-weight:{weight}}}"
        )
    };
    [
        face("AF Sans", FACE_FILES[0], 400),
        face("AF Sans", FACE_FILES[1], 600),
        face("AF Sans JP", FACE_FILES[2], 400),
        face("AF Sans JP", FACE_FILES[3], 600),
        face("AF Mono", FACE_FILES[4], 400),
    ]
    .concat()
}

/// The diagram as an SVG. With a `report`, the lines it found passing
/// through a node are drawn red, and the nodes they pass through outlined red.
pub fn svg(diagram: &Diagram, faces: Option<&str>, report: Option<&Report>) -> String {
    let bad_edges: HashSet<&str> = report
        .map(|r| {
            r.edge_node_intersections
                .iter()
                .map(|i| i.relationship.as_str())
                .collect()
        })
        .unwrap_or_default();
    let bad_nodes: HashSet<&str> = report
        .map(|r| {
            r.edge_node_intersections
                .iter()
                .map(|i| i.node.as_str())
                .chain(r.text_escapes.iter().map(|e| e.node.as_str()))
                .chain(
                    r.node_overlaps
                        .iter()
                        .flat_map(|(a, b)| [a.as_str(), b.as_str()]),
                )
                .collect()
        })
        .unwrap_or_default();
    let (sans, mono) = if faces.is_some() {
        (
            "'AF Sans', 'AF Sans JP', sans-serif",
            "'AF Mono', 'AF Sans', 'AF Sans JP', monospace",
        )
    } else {
        (
            "IBM Plex Sans, IBM Plex Sans JP, sans-serif",
            "IBM Plex Mono, IBM Plex Sans, IBM Plex Sans JP, monospace",
        )
    };

    let b = diagram.bounds;
    let mut svg = String::new();
    let _ = writeln!(
        svg,
        r#"<svg xmlns="http://www.w3.org/2000/svg" viewBox="{} {} {} {}" width="{}" height="{}" font-family="{sans}">"#,
        b.x, b.y, b.width, b.height, b.width, b.height
    );
    if let Some(folder) = faces {
        let _ = writeln!(svg, "<style>{}</style>", font_faces(folder));
    }
    let _ = writeln!(
        svg,
        r##"<rect x="{}" y="{}" width="{}" height="{}" fill="#fff"/>"##,
        b.x, b.y, b.width, b.height
    );
    for edge in &diagram.edges {
        let colour = if bad_edges.contains(edge.relationship.as_str()) {
            "#d00"
        } else {
            "#555"
        };
        let points: Vec<String> = edge
            .points
            .iter()
            .map(|p| format!("{},{}", p.x, p.y))
            .collect();
        let dash = match edge.style {
            LineStyle::Solid => "",
            LineStyle::Dashed => r#" stroke-dasharray="6 4""#,
            LineStyle::Dotted => r#" stroke-dasharray="1 3""#,
        };
        let _ = writeln!(
            svg,
            r#"<polyline points="{}" fill="none" stroke="{colour}"{dash}><title>{}</title></polyline>"#,
            points.join(" "),
            escape(&edge.relationship)
        );
        let [tip, left, right] = edge.head;
        match edge.arrowhead {
            Arrowhead::Filled => {
                let _ = writeln!(
                    svg,
                    r#"<polygon points="{},{} {},{} {},{}" fill="{colour}"/>"#,
                    tip.x, tip.y, left.x, left.y, right.x, right.y
                );
            }
            Arrowhead::Hollow => {
                let _ = writeln!(
                    svg,
                    r##"<polygon points="{},{} {},{} {},{}" fill="#fff" stroke="{colour}"/>"##,
                    tip.x, tip.y, left.x, left.y, right.x, right.y
                );
            }
            Arrowhead::Open => {
                let _ = writeln!(
                    svg,
                    r#"<polyline points="{},{} {},{} {},{}" fill="none" stroke="{colour}"/>"#,
                    left.x, left.y, tip.x, tip.y, right.x, right.y
                );
            }
        }
    }
    for node in &diagram.nodes {
        let r = node.shape.rect;
        let stroke = if bad_nodes.contains(node.id.as_str()) {
            "#d00"
        } else {
            "#333"
        };
        let style = format!(r##"fill="#fff" stroke="{stroke}""##);
        match node.shape.kind {
            ShapeKind::Rectangle => {
                let _ = writeln!(
                    svg,
                    r#"<rect x="{}" y="{}" width="{}" height="{}" {style}/>"#,
                    r.x, r.y, r.width, r.height
                );
            }
            ShapeKind::Neutral => {
                let _ = writeln!(
                    svg,
                    r#"<rect x="{}" y="{}" width="{}" height="{}" {style} stroke-dasharray="4 3"/>"#,
                    r.x, r.y, r.width, r.height
                );
            }
            ShapeKind::RoundedRectangle { radius } => {
                let _ = writeln!(
                    svg,
                    r#"<rect x="{}" y="{}" width="{}" height="{}" rx="{radius}" {style}/>"#,
                    r.x, r.y, r.width, r.height
                );
            }
            ShapeKind::Ellipse | ShapeKind::Circle => {
                let c = r.centre();
                let _ = writeln!(
                    svg,
                    r#"<ellipse cx="{}" cy="{}" rx="{}" ry="{}" {style}/>"#,
                    c.x,
                    c.y,
                    r.width / 2.0,
                    r.height / 2.0
                );
            }
            ShapeKind::Parallelogram { .. } => {
                let points: Vec<String> = node
                    .shape
                    .polygon()
                    .iter()
                    .map(|p| format!("{},{}", p.x, p.y))
                    .collect();
                let _ = writeln!(svg, r#"<polygon points="{}" {style}/>"#, points.join(" "));
            }
        }
        for decorator in &node.decorators {
            match decorator {
                Decorator::Undeveloped { centre, half } => {
                    let _ = writeln!(
                        svg,
                        r##"<polygon points="{},{} {},{} {},{} {},{}" fill="#fff" stroke="#333"/>"##,
                        centre.x,
                        centre.y - half,
                        centre.x + half,
                        centre.y,
                        centre.x,
                        centre.y + half,
                        centre.x - half,
                        centre.y
                    );
                }
                Decorator::Letter {
                    text,
                    x,
                    baseline,
                    size,
                } => {
                    let _ = writeln!(
                        svg,
                        r#"<text x="{x}" y="{baseline}" font-size="{size}" font-weight="600">{text}</text>"#
                    );
                }
            }
        }
        for line in &node.text {
            let weight = if line.weight == Weight::SemiBold {
                600
            } else {
                400
            };
            let (fill, family) = if matches!(line.role, TextRole::Identifier | TextRole::Package) {
                ("#666", format!(r#" font-family="{mono}""#))
            } else {
                ("#111", String::new())
            };
            let _ = writeln!(
                svg,
                r#"<text x="{}" y="{}" font-size="{}" font-weight="{weight}" fill="{fill}"{family}>{}</text>"#,
                line.x,
                line.baseline,
                line.size,
                escape(&line.text)
            );
        }
    }
    svg.push_str("</svg>\n");
    svg
}

fn escape(text: &str) -> String {
    text.replace('&', "&amp;")
        .replace('<', "&lt;")
        .replace('"', "&quot;")
}
