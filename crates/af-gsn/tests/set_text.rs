//! Changing a node's identifier, name and statement against the writing
//! rules W15 to W17 of `docs/standards/sacm-gsn-mapping.md`: what is set reads
//! back, by the reading rules T1 to T3, as what was given, in the document
//! and in the file written from it.

use af_gsn::commands::{AddError, Refusal, TextField, TextSet, set_text};
use af_gsn::view::{Node, View, view};
use sacm::model::Document;
use sacm::xmi::{self, Mode, ReadOptions, WriteOptions};

const SAMPLES: [&str; 6] = [
    "sacm_example_core_argument.sacm.xmi",
    "sample.sacm.xmi",
    "open-autonomy-safety-case.updated.v2.sacm.xmi",
    "oasc-ja.sacm.xmi",
    "safety-case-sanitized-strict.sacm.xmi",
    "challenges.sacm.xmi",
];

fn read(bytes: &[u8]) -> Document {
    match xmi::read(bytes, &ReadOptions::default().with_mode(Mode::Strict)) {
        Ok(output) => output.document().clone(),
        Err(error) => panic!("setup: a strict read refused the file: {error:?}"),
    }
}

fn sample(name: &str) -> Document {
    let path = format!("{}/../../testdata/sacm/{name}", env!("CARGO_MANIFEST_DIR"));
    let bytes = std::fs::read(&path).unwrap_or_else(|error| panic!("setup: {path}: {error}"));
    read(&bytes)
}

/// The document as the file written from it reads.
fn written(document: &Document) -> Document {
    let bytes = xmi::write(document, &WriteOptions::default())
        .unwrap_or_else(|error| panic!("the document is not written: {error:?}"));
    read(bytes.as_ref())
}

/// An AssuranceCasePackage holding one ArgumentPackage with `body`.
fn case(body: &str) -> Document {
    let text = format!(
        r#"<?xml version="1.0" encoding="UTF-8"?>
<sacm:AssuranceCasePackage xmlns:sacm="http://www.omg.org/spec/SACM/20220301" xmlns:xmi="http://www.omg.org/spec/XMI/20131001" xmi:id="case">
  <name content="Case"/>
  <argumentPackage xmi:id="p"><name content="P"/>
    {body}
  </argumentPackage>
</sacm:AssuranceCasePackage>"#
    );
    read(text.as_bytes())
}

/// Two goals joined by an inference: `g1` with a name in English, an
/// identifier tag and a description in English and Japanese; `g2` with a
/// name alone.
fn two_goals() -> Document {
    case(
        r#"<argumentElement xmi:type="sacm:Claim" xmi:id="g1">
      <name lang="en" content="Safe"/>
      <description xmi:id="d1"><content><value lang="en" content="The system is safe"/><value lang="ja" content="システムは安全である"/></content></description>
      <taggedValue xmi:id="t1"><key><value content="assuranceForge.gsn.identifier"/></key><content><value content="G1"/></content></taggedValue>
    </argumentElement>
    <argumentElement xmi:type="sacm:Claim" xmi:id="g2"><name content="Tested"/></argumentElement>
    <argumentElement xmi:type="sacm:AssertedInference" xmi:id="i1" source="g2" target="g1"><name content=""/></argumentElement>"#,
    )
}

fn node<'a>(view: &'a View, id: &str) -> &'a Node {
    view.nodes
        .iter()
        .find(|node| node.id == id)
        .unwrap_or_else(|| panic!("no node {id}"))
}

fn set(document: &mut Document, id: &str, field: TextField, text: &str, language: &str) -> TextSet {
    let before = view(document, language);
    set_text(document, &before, id, field, text, language)
        .unwrap_or_else(|error| panic!("{id} {field:?}: {error}"))
}

fn refusal(document: &mut Document, id: &str, field: TextField, text: &str) -> Refusal {
    let before = view(document, "en");
    let held = document.clone();
    let result = set_text(document, &before, id, field, text, "en");
    assert!(*document == held, "a refusal changes nothing");
    match result {
        Err(AddError::Refused(refusal)) => refusal,
        other => panic!("{id} {field:?} {text:?} was not refused: {other:?}"),
    }
}

#[test]
fn the_fixture_reads_as_the_tests_expect() {
    let document = two_goals();
    let en = view(&document, "en");
    assert_eq!(node(&en, "g1").identifier, "G1");
    assert_eq!(node(&en, "g1").name.as_deref(), Some("Safe"));
    assert_eq!(
        node(&en, "g1").statement.as_deref(),
        Some("The system is safe")
    );
    assert_eq!(node(&en, "g2").identifier, "g2");
    assert_eq!(node(&en, "g2").statement, None);
}

#[test]
fn w16_a_name_is_set_and_keeps_its_language() {
    let mut document = two_goals();
    assert!(matches!(
        set(
            &mut document,
            "g1",
            TextField::Name,
            "Acceptably safe",
            "en"
        ),
        TextSet::Changed(_)
    ));
    for document in [document.clone(), written(&document)] {
        let name = document.get("g1").and_then(|g1| g1.name()).cloned();
        let name = name.unwrap_or_default();
        assert_eq!(name.content(), Some("Acceptably safe"));
        assert_eq!(name.lang(), Some("en"));
        assert_eq!(
            node(&view(&document, "en"), "g1").name.as_deref(),
            Some("Acceptably safe")
        );
    }
}

#[test]
fn w16_a_name_can_be_emptied() {
    let mut document = two_goals();
    set(&mut document, "g2", TextField::Name, "", "en");
    let name = written(&document)
        .get("g2")
        .and_then(|g2| g2.name())
        .cloned()
        .unwrap_or_default();
    assert_eq!(name.content().unwrap_or_default(), "");
    assert_eq!(name.lang(), None, "a name the file held keeps no language");
    // Written again where there is now none, it is taken to be English.
    set(&mut document, "g2", TextField::Name, "Tested again", "en");
    let name = written(&document)
        .get("g2")
        .and_then(|g2| g2.name())
        .cloned()
        .unwrap_or_default();
    assert_eq!(name.content(), Some("Tested again"));
    assert_eq!(name.lang(), Some("en"));
}

#[test]
fn w17_a_statement_is_set_in_the_language_shown_and_the_others_stay() {
    let mut document = two_goals();
    set(
        &mut document,
        "g1",
        TextField::Statement,
        "システムは十分に安全である",
        "ja",
    );
    for document in [document.clone(), written(&document)] {
        assert_eq!(
            node(&view(&document, "ja"), "g1").statement.as_deref(),
            Some("システムは十分に安全である")
        );
        assert_eq!(
            node(&view(&document, "en"), "g1").statement.as_deref(),
            Some("The system is safe"),
            "the English value is as it was"
        );
        assert_eq!(
            document.get("g1").map(|g1| g1.descriptions().count()),
            Some(1)
        );
    }
}

#[test]
fn w17_a_statement_shown_from_another_language_is_the_one_set() {
    // French is not in the file: T3 shows the first value, the English one.
    let mut document = two_goals();
    set(&mut document, "g1", TextField::Statement, "Sûr", "fr");
    assert_eq!(
        node(&view(&document, "en"), "g1").statement.as_deref(),
        Some("Sûr")
    );
    assert_eq!(
        node(&view(&document, "ja"), "g1").statement.as_deref(),
        Some("システムは安全である")
    );
}

#[test]
fn w17_a_statement_with_no_description_makes_one() {
    let mut document = two_goals();
    let TextSet::Changed(mutation) = set(
        &mut document,
        "g2",
        TextField::Statement,
        "Every test passed.\nOn the bench.",
        "en",
    ) else {
        panic!("nothing changed");
    };
    assert_eq!(mutation.created_ids().len(), 1);
    for document in [document.clone(), written(&document)] {
        assert_eq!(
            node(&view(&document, "en"), "g2").statement.as_deref(),
            Some("Every test passed.\nOn the bench.")
        );
        // English is assumed for a text first written here.
        let langs: Vec<Option<String>> = document
            .get("g2")
            .and_then(|g2| g2.descriptions().next())
            .and_then(|description| description.content())
            .map(|content| {
                content
                    .values()
                    .iter()
                    .map(|value| value.lang().map(str::to_owned))
                    .collect()
            })
            .unwrap_or_default();
        assert_eq!(langs, [Some("en".to_owned())]);
    }
}

#[test]
fn w17_an_emptied_statement_reads_back_as_what_the_file_still_holds() {
    let mut document = two_goals();
    set(&mut document, "g1", TextField::Statement, "", "en");
    // The English value is empty, so T3 reads the next: the Japanese one.
    assert_eq!(
        node(&view(&document, "en"), "g1").statement.as_deref(),
        Some("システムは安全である")
    );
    // An element with no description gets none for an empty statement.
    assert!(matches!(
        set(&mut document, "g2", TextField::Statement, "", "en"),
        TextSet::Unchanged
    ));
    assert_eq!(
        document.get("g2").map(|g2| g2.descriptions().count()),
        Some(0)
    );
}

#[test]
fn w15_an_identifier_is_set_in_its_tag_or_given_one() {
    let mut document = two_goals();
    set(&mut document, "g1", TextField::Identifier, "G-TOP", "en");
    let TextSet::Changed(mutation) = set(&mut document, "g2", TextField::Identifier, "G2", "en")
    else {
        panic!("nothing changed");
    };
    assert_eq!(mutation.created_ids().len(), 1, "g2 had no tag");
    for document in [document.clone(), written(&document)] {
        let after = view(&document, "en");
        assert_eq!(node(&after, "g1").identifier, "G-TOP");
        assert_eq!(node(&after, "g2").identifier, "G2");
        assert_eq!(
            document.get("g1").map(|g1| g1.tagged_values().count()),
            Some(1),
            "the tag is changed, not made again"
        );
        // The SACM ids, which the relationship names, are as they were.
        assert_eq!(after.edges.len(), 1);
    }
}

#[test]
fn w15_an_identifier_that_is_empty_or_taken_is_refused() {
    let mut document = two_goals();
    for empty in ["", "  "] {
        assert_eq!(
            refusal(&mut document, "g1", TextField::Identifier, empty),
            Refusal::IdentifierEmpty
        );
    }
    // Another node's identifier: its tag, or its SACM id.
    assert_eq!(
        refusal(&mut document, "g2", TextField::Identifier, "G1"),
        Refusal::IdentifierTaken
    );
    assert_eq!(
        refusal(&mut document, "g1", TextField::Identifier, "g2"),
        Refusal::IdentifierTaken
    );
    // An element the view does not draw as a node keeps its id to itself.
    assert_eq!(
        refusal(&mut document, "g1", TextField::Identifier, "i1"),
        Refusal::IdentifierTaken
    );
    // Its own SACM id is its own.
    assert!(matches!(
        set(&mut document, "g1", TextField::Identifier, "g1", "en"),
        TextSet::Changed(_)
    ));
}

#[test]
fn the_text_it_already_has_changes_nothing() {
    let mut document = two_goals();
    let held = document.clone();
    for (id, field, text) in [
        ("g1", TextField::Identifier, "G1"),
        ("g1", TextField::Name, "Safe"),
        ("g1", TextField::Statement, "The system is safe"),
        ("g2", TextField::Name, "Tested"),
    ] {
        assert!(
            matches!(
                set(&mut document, id, field, text, "en"),
                TextSet::Unchanged
            ),
            "{id} {field:?}"
        );
    }
    assert!(document == held);
}

#[test]
fn only_a_node_of_the_diagram_has_its_text_set() {
    let mut document = two_goals();
    for id in ["i1", "p", "d1", "nothing"] {
        assert_eq!(
            refusal(&mut document, id, TextField::Name, "x"),
            Refusal::NotANode
        );
    }
}

/// On a few nodes of every sample, in both languages: each text reads back
/// as set, in the document and in the file written from it; nothing else of
/// the view changes; and the document is as valid as it was.
#[test]
fn every_text_reads_back_as_set_on_every_sample() {
    let mut checked = 0;
    for name in SAMPLES {
        let document = sample(name);
        let diagnostics = sacm::validation::validate(&document).len();
        for language in ["en", "ja"] {
            let before = view(&document, language);
            let step = (before.nodes.len() / 6).max(1);
            for target in before.nodes.iter().step_by(step) {
                for (field, text) in [
                    (TextField::Identifier, "Z-new-1"),
                    (TextField::Name, "A new name"),
                    (TextField::Statement, "A new statement.\n二行目。"),
                ] {
                    let what = format!("{name} {language} {} {field:?}", target.id);
                    let mut edited = document.clone();
                    let result = set_text(&mut edited, &before, &target.id, field, text, language);
                    if matches!(result, Err(AddError::Refused(Refusal::TextIsExpression))) {
                        assert!(edited == document, "{what}");
                        continue;
                    }
                    assert!(
                        matches!(result, Ok(TextSet::Changed(_))),
                        "{what}: {result:?}"
                    );
                    assert_eq!(
                        sacm::validation::validate(&edited).len(),
                        diagnostics,
                        "{what}"
                    );
                    for edited in [edited.clone(), written(&edited)] {
                        let after = view(&edited, language);
                        assert_eq!(after.nodes.len(), before.nodes.len(), "{what}");
                        assert_eq!(after.edges, before.edges, "{what}");
                        assert_eq!(after.findings, before.findings, "{what}");
                        for (was, is) in before.nodes.iter().zip(&after.nodes) {
                            let mut expected = was.clone();
                            if was.id == target.id {
                                match field {
                                    TextField::Identifier => expected.identifier = text.to_owned(),
                                    TextField::Name => expected.name = Some(text.to_owned()),
                                    TextField::Statement => {
                                        expected.statement = Some(text.to_owned());
                                    }
                                }
                            }
                            assert_eq!(*is, expected, "{what}");
                        }
                    }
                    checked += 1;
                }
            }
        }
    }
    assert!(checked > 100, "only {checked} texts were set");
}
