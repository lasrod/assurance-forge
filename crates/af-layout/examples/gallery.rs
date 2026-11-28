//! The layout gallery: lays out each fixture, checks its geometry, and writes
//! an SVG of each and a table of what the checks found. It is how a layout
//! change is judged: run it before and after, and compare.
//!
//! ```text
//! cargo run --release -p af-layout --example gallery -- <out-dir> [--fonts <dir>]
//! ```
//!
//! `--fonts` names a folder with DATUM's measured faces (`FACE_FILES` in
//! `common/svg.rs`), such as `ui/node_modules/@datum-ui/fonts/dist/measured`,
//! so the layout is the one the application makes and the SVGs draw with the
//! same faces. Without it, the test faces in `testdata/fonts` are used, whose
//! Japanese is only a subset.
//!
//! It writes `<name>.svg` for each fixture, `gallery.json` with each
//! fixture's measurements, and `gallery.md`, the same as a table. It fails
//! when a fixture's geometry is invalid (`af_layout::Report::is_valid`).

use std::fmt::Write as _;
use std::process::ExitCode;
use std::time::Instant;

use af_layout::{Engine, Fonts, Style};
use sacm::xmi::{self, Mode, ReadOptions};

#[path = "common/svg.rs"]
mod svg;

/// The gallery: file in `testdata/sacm`, the language its text is shown in,
/// and what it is there to show.
const FIXTURES: [(&str, &str, &str); 11] = [
    (
        "sacm_example_core_argument.sacm.xmi",
        "en",
        "an ordinary, readable argument",
    ),
    (
        "example.sacm.xmi",
        "en",
        "the welcome page's example: every element and mark the editor supports",
    ),
    (
        "gallery-shared-support.sacm.xmi",
        "en",
        "support and evidence shared at different depths",
    ),
    (
        "gallery-attachments.sacm.xmi",
        "en",
        "several context, assumption and justification attachments",
    ),
    (
        "gallery-long-text.sacm.xmi",
        "en",
        "long names and statements, unbroken identifiers",
    ),
    (
        "gallery-japanese.sacm.xmi",
        "ja",
        "Japanese and mixed-language text",
    ),
    (
        "challenges.sacm.xmi",
        "en",
        "challenges of nodes and of relationships",
    ),
    (
        "acps.sacm.xmi",
        "en",
        "assurance claim points: a description, a confidence argument elsewhere, a local confidence goal",
    ),
    (
        "gallery-roots-and-cycle.sacm.xmi",
        "en",
        "several roots and a cycle",
    ),
    ("oasc-ja.sacm.xmi", "ja", "a realistic case in Japanese"),
    (
        "safety-case-sanitized-strict.sacm.xmi",
        "en",
        "the large realistic case",
    ),
];

/// Layouts timed for each fixture; the median is reported.
const RUNS: usize = 5;

fn read(path: &str) -> Result<Vec<u8>, String> {
    std::fs::read(path).map_err(|error| format!("{path}: {error}"))
}

fn run() -> Result<(), String> {
    let mut args = std::env::args().skip(1);
    let mut out = None;
    let mut faces = None;
    while let Some(arg) = args.next() {
        if arg == "--fonts" {
            faces = Some(args.next().ok_or("--fonts needs a folder")?);
        } else {
            out = Some(arg);
        }
    }
    let out = out.ok_or("usage: gallery <out-dir> [--fonts <dir>]")?;
    let root = concat!(env!("CARGO_MANIFEST_DIR"), "/../..");
    let test_faces = [
        "IBMPlexSans-Regular.ttf",
        "IBMPlexSans-SemiBold.ttf",
        "AFTestSansJP-Regular.ttf",
        "AFTestSansJP-SemiBold.ttf",
        "IBMPlexMono-Regular.ttf",
    ];
    let paths: Vec<String> = match &faces {
        Some(dir) => svg::FACE_FILES
            .iter()
            .map(|file| format!("{dir}/{file}"))
            .collect(),
        None => test_faces
            .iter()
            .map(|file| format!("{root}/testdata/fonts/{file}"))
            .collect(),
    };
    let bytes: Vec<Vec<u8>> = paths.iter().map(|p| read(p)).collect::<Result<_, _>>()?;
    std::fs::create_dir_all(format!("{out}/fonts")).map_err(|e| format!("{out}: {e}"))?;
    for (bytes, file) in bytes.iter().zip(svg::FACE_FILES) {
        std::fs::write(format!("{out}/fonts/{file}"), bytes).map_err(|e| format!("{out}: {e}"))?;
    }
    let fonts = || {
        Fonts::new(
            bytes[0].clone(),
            bytes[1].clone(),
            bytes[2].clone(),
            bytes[3].clone(),
            bytes[4].clone(),
        )
        .map_err(|error| format!("{error:?}"))
    };

    let mut json = String::from("[\n");
    let mut table = String::new();
    let _ = writeln!(
        table,
        "Fonts: {}\n\n| Fixture | Shows | Nodes | Edges | Overlaps | Lines through nodes | Text outside | Edge crossings | Unplaced edges | Size (px) | Layout, cold (ms) | Layout, warm (ms) |\n|---|---|--:|--:|--:|--:|--:|--:|--:|--:|--:|--:|",
        if faces.is_some() {
            "DATUM's measured faces"
        } else {
            "test faces (Japanese subset)"
        }
    );
    let mut invalid = 0;
    for (i, (file, language, shows)) in FIXTURES.iter().enumerate() {
        let bytes = read(&format!("{root}/testdata/sacm/{file}"))?;
        let document = xmi::read(&bytes, &ReadOptions::default().with_mode(Mode::Strict))
            .map_err(|error| format!("{file}: refused: {}", error.code().as_str()))?;
        let view = af_gsn::view::view(document.document(), language);

        // Cold: a new engine, nothing measured yet. Warm: the same engine
        // again, as when the view is laid out a second time.
        let mut cold = Vec::with_capacity(RUNS);
        let mut warm = Vec::with_capacity(RUNS);
        let mut diagram = None;
        for _ in 0..RUNS {
            let mut engine = Engine::new(fonts()?, Style::default());
            let start = Instant::now();
            let first = engine.layout(&view);
            cold.push(start.elapsed().as_secs_f64() * 1000.0);
            let start = Instant::now();
            let _ = engine.layout(&view);
            warm.push(start.elapsed().as_secs_f64() * 1000.0);
            diagram = Some(first);
        }
        let Some(diagram) = diagram else {
            return Err("no layout".to_owned());
        };
        let median = |times: &mut Vec<f64>| {
            times.sort_by(f64::total_cmp);
            times[times.len() / 2]
        };
        let (cold, warm) = (median(&mut cold), median(&mut warm));
        let report = af_layout::check(&diagram);
        if !report.is_valid() {
            invalid += 1;
        }
        let name = file.trim_end_matches(".sacm.xmi");
        std::fs::write(
            format!("{out}/{name}.svg"),
            svg::svg(&diagram, Some("fonts"), Some(&report)),
        )
        .map_err(|e| format!("{out}: {e}"))?;

        let through: Vec<String> = report
            .edge_node_intersections
            .iter()
            .map(|i| {
                format!(
                    "{{\"relationship\":{:?},\"node\":{:?}}}",
                    i.relationship, i.node
                )
            })
            .collect();
        let _ = writeln!(
            json,
            "  {{\"fixture\":{file:?},\"language\":{language:?},\"nodes\":{},\"edges\":{},\"nodeOverlaps\":{},\"edgeNodeIntersections\":[{}],\"textEscapes\":{},\"edgeCrossings\":{},\"unplacedEdges\":{},\"edgeLength\":{:.0},\"width\":{:.0},\"height\":{:.0},\"layoutColdMs\":{cold:.2},\"layoutWarmMs\":{warm:.2}}}{}\n",
            report.nodes,
            report.edges,
            report.node_overlaps.len(),
            through.join(","),
            report.text_escapes.len(),
            report.edge_crossings,
            report.unplaced_edges,
            report.edge_length,
            report.width,
            report.height,
            if i + 1 < FIXTURES.len() { "," } else { "" }
        );
        let _ = writeln!(
            table,
            "| [{name}]({name}.svg) | {shows} | {} | {} | {} | {} | {} | {} | {} | {:.0} × {:.0} | {cold:.1} | {warm:.1} |",
            report.nodes,
            report.edges,
            report.node_overlaps.len(),
            report.edge_node_intersections.len(),
            report.text_escapes.len(),
            report.edge_crossings,
            report.unplaced_edges,
            report.width,
            report.height
        );
        println!(
            "{name}: {} nodes, {} edges, {} through nodes, {} crossings, {cold:.1} ms",
            report.nodes,
            report.edges,
            report.edge_node_intersections.len(),
            report.edge_crossings
        );
    }
    json.push_str("]\n");
    std::fs::write(format!("{out}/gallery.json"), json).map_err(|e| format!("{out}: {e}"))?;
    std::fs::write(format!("{out}/gallery.md"), table).map_err(|e| format!("{out}: {e}"))?;
    if invalid > 0 {
        return Err(format!(
            "{invalid} fixture(s) have invalid geometry: see {out}/gallery.md"
        ));
    }
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
