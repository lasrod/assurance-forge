//! A development preview: lays out a SACM file and writes the render model as
//! a plain SVG, to look at the layout before the application draws it. See
//! `examples/common/svg.rs` for what it draws; `gallery` does the same for
//! every gallery fixture, with the geometry checks.
//!
//! ```text
//! cargo run -p af-layout --example preview -- testdata/sacm/sacm_example_core_argument.sacm.xmi out.svg
//! ```

use std::process::ExitCode;

use af_layout::{Engine, Fonts, Style};
use sacm::xmi::{self, Mode, ReadOptions};

#[path = "common/svg.rs"]
mod svg;

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
    let report = af_layout::check(&diagram);
    std::fs::write(&output, svg::svg(&diagram, None, Some(&report)))
        .map_err(|error| format!("{output}: {error}"))?;
    println!(
        "{output}: {} nodes, {} edges, {} lines through nodes",
        diagram.nodes.len(),
        diagram.edges.len(),
        report.edge_node_intersections.len()
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
