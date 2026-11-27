//! A package's diagram (A5 and A6 of `docs/roadmap/milestone-2-acps.md`):
//! its nodes, every edge with an end among them, and the nodes of other
//! packages those edges reach, marked with their package.

use std::collections::HashSet;

use af_gsn::view::{View, of_package, view};
use sacm::model::{Document, ElementKind};
use sacm::xmi::{self, Mode, ReadOptions};

fn sample(name: &str) -> Document {
    let path = format!("{}/../../testdata/sacm/{name}", env!("CARGO_MANIFEST_DIR"));
    let bytes = std::fs::read(&path).unwrap_or_else(|error| panic!("setup: {path}: {error}"));
    match xmi::read(&bytes, &ReadOptions::default().with_mode(Mode::Strict)) {
        Ok(output) => output.document().clone(),
        Err(error) => panic!("setup: {name}: {error:?}"),
    }
}

fn package(shown: &View, id: &str) -> Option<Option<String>> {
    shown.nodes.iter().find(|node| node.id == id).map(|node| {
        node.elsewhere
            .as_ref()
            .map(|elsewhere| elsewhere.name.clone())
    })
}

#[test]
fn a_package_draws_its_nodes_and_marks_the_ones_its_lines_reach_elsewhere() {
    let document = sample("oasc-ja.sacm.xmi");
    let whole = view(&document, "en");
    let vehicle = of_package(&document, &whole, "ARG_VehicleSafety")
        .unwrap_or_else(|| panic!("ARG_VehicleSafety is a package"));
    // G37 is the package's own, and supports G4 of the main argument.
    assert_eq!(package(&vehicle, "G37"), Some(None));
    assert_eq!(
        package(&vehicle, "G4"),
        Some(Some("Open Autonomy Safety Case - Main Argument".to_owned()))
    );
    assert!(
        vehicle
            .edges
            .iter()
            .any(|edge| edge.relationship == "AI_G37_G4")
    );
    // The main argument shows the same line from its side.
    let main = of_package(&document, &whole, "ARG_Main")
        .unwrap_or_else(|| panic!("ARG_Main is a package"));
    // The inference runs through the Strategy S33, of Vehicle Safety: the
    // main argument draws the line to it, marked, and nothing beyond.
    assert_eq!(package(&main, "G4"), Some(None));
    assert_eq!(
        package(&main, "S33"),
        Some(Some("Vehicle Safety".to_owned()))
    );
    assert_eq!(package(&main, "G37"), None);
    // Fewer nodes than the whole case: the other packages' own are not drawn.
    assert!(main.nodes.len() < whole.nodes.len());
    // The whole case marks no node.
    assert!(whole.nodes.iter().all(|node| node.elsewhere.is_none()));
}

#[test]
fn only_an_argument_package_has_a_diagram() {
    let document = sample("oasc-ja.sacm.xmi");
    let whole = view(&document, "en");
    assert!(of_package(&document, &whole, "G4").is_none());
    assert!(of_package(&document, &whole, "no such id").is_none());
}

/// In every sample, each package's diagram has only edges with an end among
/// its own nodes, and nodes those edges reach; and each line of the whole
/// case is drawn in some package, so no line is lost to the split.
#[test]
fn in_each_sample_the_packages_together_draw_every_line() {
    for name in [
        "oasc-ja.sacm.xmi",
        "safety-case-sanitized-strict.sacm.xmi",
        "sacm_example_core_argument.sacm.xmi",
        "challenges.sacm.xmi",
    ] {
        let document = sample(name);
        let whole = view(&document, "en");
        let packages: Vec<String> = document
            .elements()
            .filter(|element| element.kind() == ElementKind::ArgumentPackage)
            .map(|element| element.id().as_str().to_owned())
            .collect();
        assert!(!packages.is_empty(), "{name}");
        let mut drawn: HashSet<(String, String, String)> = HashSet::new();
        for id in &packages {
            let shown = of_package(&document, &whole, id)
                .unwrap_or_else(|| panic!("{name}: {id} is a package"));
            let ids: HashSet<&str> = shown.nodes.iter().map(|node| node.id.as_str()).collect();
            let own: HashSet<&str> = shown
                .nodes
                .iter()
                .filter(|node| node.elsewhere.is_none())
                .map(|node| node.id.as_str())
                .collect();
            let relationships: HashSet<&str> = shown
                .edges
                .iter()
                .map(|edge| edge.relationship.as_str())
                .collect();
            for edge in &shown.edges {
                assert!(ids.contains(edge.from.as_str()), "{name}: {id}: {edge:?}");
                if edge.to_relationship {
                    assert!(
                        relationships.contains(edge.to.as_str()),
                        "{name}: {id}: {edge:?}"
                    );
                } else {
                    assert!(ids.contains(edge.to.as_str()), "{name}: {id}: {edge:?}");
                    assert!(
                        own.contains(edge.from.as_str())
                            || own.contains(edge.to.as_str())
                            || relationships.contains(edge.to.as_str()),
                        "{name}: {id}: {edge:?} has no end of the package"
                    );
                }
                drawn.insert((
                    edge.relationship.clone(),
                    edge.from.clone(),
                    edge.to.clone(),
                ));
            }
        }
        for edge in &whole.edges {
            let key = (
                edge.relationship.clone(),
                edge.from.clone(),
                edge.to.clone(),
            );
            assert!(
                drawn.contains(&key),
                "{name}: {edge:?} is drawn in no package"
            );
        }
    }
}
