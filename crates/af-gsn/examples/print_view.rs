//! Prints the GSN view of a SACM 2.3 XMI file, to try the reading rules on
//! real files before there is a diagram.
//!
//! ```text
//! cargo run -p af-gsn --example print_view -- testdata/sacm/sacm_example_core_argument.sacm.xmi
//! cargo run -p af-gsn --example print_view -- testdata/sacm/oasc-ja.sacm.xmi ja
//! ```
//!
//! The second argument is the interface language (default `en`). The output
//! is for a developer: the application words codes in the frontend.

use std::process::ExitCode;

use af_gsn::view::{EdgeKind, view};
use sacm::xmi::{self, Mode, ReadOptions};

fn main() -> ExitCode {
    let mut args = std::env::args().skip(1);
    let Some(path) = args.next() else {
        eprintln!("usage: print_view <file.sacm.xmi> [language]");
        return ExitCode::from(2);
    };
    let language = args.next().unwrap_or_else(|| "en".to_owned());

    let bytes = match std::fs::read(&path) {
        Ok(bytes) => bytes,
        Err(error) => {
            eprintln!("{path}: {error}");
            return ExitCode::from(2);
        }
    };
    let output = match xmi::read(&bytes, &ReadOptions::default().with_mode(Mode::Strict)) {
        Ok(output) => output,
        Err(error) => {
            eprintln!("refused: {} {error:?}", error.code().as_str());
            return ExitCode::FAILURE;
        }
    };
    let view = view(output.document(), &language);

    println!("Nodes ({})", view.nodes.len());
    for node in &view.nodes {
        let decorator = if node.undeveloped {
            ", undeveloped"
        } else {
            ""
        };
        println!(
            "  {:<13} {}{decorator}  {}",
            format!("{:?}", node.kind),
            node.identifier,
            node.name.as_deref().unwrap_or("")
        );
        if let Some(statement) = &node.statement {
            println!("                {statement}");
        }
    }
    println!("Edges ({})", view.edges.len());
    for edge in &view.edges {
        let arrow = match edge.kind {
            EdgeKind::SupportedBy => "SupportedBy",
            EdgeKind::InContextOf => "InContextOf",
            EdgeKind::Challenge => "Challenge  ",
            EdgeKind::Neutral => "Neutral    ",
        };
        println!(
            "  {arrow} {} -> {}  (via {})",
            edge.from, edge.to, edge.relationship
        );
    }
    if !view.unplaced.is_empty() {
        println!("Not placed ({})", view.unplaced.len());
        for item in &view.unplaced {
            println!("  {} {}", item.code.as_str(), item.id);
        }
    }
    if !view.findings.is_empty() {
        println!("Codes ({})", view.findings.len());
        for finding in &view.findings {
            match &finding.end {
                Some(end) => println!(
                    "  {} {} (end {end})",
                    finding.code.as_str(),
                    finding.element
                ),
                None => println!("  {} {}", finding.code.as_str(), finding.element),
            }
        }
    }
    println!(
        "Read: {} diagnostics from libSACM",
        output.diagnostics().len()
    );
    ExitCode::SUCCESS
}
