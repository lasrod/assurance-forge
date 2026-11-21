//! A development preview: lays out a SACM file and writes the render model as
//! a plain SVG, to look at the layout before the application draws it.
//!
//! This is not the SVG export, which is a later step and will be written from
//! the same render model with the faces embedded. It draws only enough to
//! judge the geometry, and names the measured faces, so it matches the layout
//! only where those faces are installed.
//!
//! ```text
//! cargo run -p af-layout --example preview -- testdata/sacm/sacm_example_core_argument.sacm.xmi out.svg
//! ```

use std::fmt::Write as _;
use std::process::ExitCode;

use af_layout::{
    Arrowhead, Decorator, Engine, Fonts, LineStyle, ShapeKind, Style, TextRole, Weight,
};
use sacm::xmi::{self, Mode, ReadOptions};

fn read(path: &str) -> Result<Vec<u8>, String> {
    std::fs::read(path).map_err(|error| format!("{path}: {error}"))
}

fn run() -> Result<(), String> {
    let mut args = std::env::args().skip(1);
    let (Some(input), Some(output)) = (args.next(), args.next()) else {
        return Err("usage: preview <file.sacm.xmi> <out.svg> [language]".to_owned());
    };
    let language = args.next().unwrap_or_else(|| "en".to_owned());
    let fonts_dir = concat!(env!("CARGO_MANIFEST_DIR"), "/../../testdata/fonts");
    let fonts = Fonts::new(
        read(&format!("{fonts_dir}/IBMPlexSans-Regular.ttf"))?,
        read(&format!("{fonts_dir}/IBMPlexSans-SemiBold.ttf"))?,
        read(&format!("{fonts_dir}/AFTestSansJP-Regular.ttf"))?,
        read(&format!("{fonts_dir}/AFTestSansJP-SemiBold.ttf"))?,
        read(&format!("{fonts_dir}/IBMPlexMono-Regular.ttf"))?,
    )
    .map_err(|error| format!("{error:?}"))?;
    let bytes = read(&input)?;
    let document = xmi::read(&bytes, &ReadOptions::default().with_mode(Mode::Strict))
        .map_err(|error| format!("refused: {}", error.code().as_str()))?;
    let view = af_gsn::view::view(document.document(), &language);
    let diagram = Engine::new(fonts, Style::default()).layout(&view);

    let b = diagram.bounds;
    let mut svg = String::new();
    let _ = writeln!(
        svg,
        r#"<svg xmlns="http://www.w3.org/2000/svg" viewBox="{} {} {} {}" width="{}" height="{}" font-family="IBM Plex Sans, IBM Plex Sans JP, sans-serif">"#,
        b.x, b.y, b.width, b.height, b.width, b.height
    );
    let _ = writeln!(
        svg,
        r##"<rect x="{}" y="{}" width="{}" height="{}" fill="#fff"/>"##,
        b.x, b.y, b.width, b.height
    );
    for edge in &diagram.edges {
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
            r##"<polyline points="{}" fill="none" stroke="#555"{dash}/>"##,
            points.join(" ")
        );
        let [tip, left, right] = edge.head;
        match edge.arrowhead {
            Arrowhead::Filled => {
                let _ = writeln!(
                    svg,
                    r##"<polygon points="{},{} {},{} {},{}" fill="#555"/>"##,
                    tip.x, tip.y, left.x, left.y, right.x, right.y
                );
            }
            Arrowhead::Hollow => {
                let _ = writeln!(
                    svg,
                    r##"<polygon points="{},{} {},{} {},{}" fill="#fff" stroke="#555"/>"##,
                    tip.x, tip.y, left.x, left.y, right.x, right.y
                );
            }
            Arrowhead::Open => {
                let _ = writeln!(
                    svg,
                    r##"<polyline points="{},{} {},{} {},{}" fill="none" stroke="#555"/>"##,
                    left.x, left.y, tip.x, tip.y, right.x, right.y
                );
            }
        }
    }
    for node in &diagram.nodes {
        let r = node.shape.rect;
        let style = r##"fill="#fff" stroke="#333""##;
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
            let (fill, family) = if line.role == TextRole::Identifier {
                (
                    "#666",
                    r#" font-family="IBM Plex Mono, IBM Plex Sans, IBM Plex Sans JP, monospace""#,
                )
            } else {
                ("#111", "")
            };
            let text = line.text.replace('&', "&amp;").replace('<', "&lt;");
            let _ = writeln!(
                svg,
                r#"<text x="{}" y="{}" font-size="{}" font-weight="{weight}" fill="{fill}"{family} textLength="{}">{text}</text>"#,
                line.x, line.baseline, line.size, line.width
            );
        }
    }
    svg.push_str("</svg>\n");
    std::fs::write(&output, svg).map_err(|error| format!("{output}: {error}"))?;
    println!(
        "{output}: {} nodes, {} edges",
        diagram.nodes.len(),
        diagram.edges.len()
    );
    Ok(())
}

fn main() -> ExitCode {
    match run() {
        Ok(()) => ExitCode::SUCCESS,
        Err(error) => {
            eprintln!("{error}");
            ExitCode::FAILURE
        }
    }
}
