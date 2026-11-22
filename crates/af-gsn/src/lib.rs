//! Goal Structuring Notation over SACM.
//!
//! This crate owns the GSN reading of a SACM document: the view derived from
//! it (nodes, relationships, decorators, identifiers), the GSN commands that
//! expand into SACM operations, and the GSN well-formedness checks.
//!
//! It holds no state. The SACM document is the only model; nothing derived
//! here is written back to it except through SACM operations.
//!
//! It does no I/O and must build for `wasm32-unknown-unknown`.
//!
//! Built so far: the view, [`view::view`], which follows the reading rules in
//! `docs/standards/sacm-gsn-mapping.md` ("Reading a SACM 2.3 file as GSN"),
//! and the commands that add elements, [`commands`], which follow its writing
//! rules ("Writing GSN as SACM").
//! Rule ids in comments (R1, E5a, T3 and so on) are that section's.

pub mod commands;
pub mod view;

/// The GSN element kinds of the GSN Community Standard Version 3 core notation.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
#[non_exhaustive]
pub enum ElementKind {
    Goal,
    Strategy,
    Solution,
    Context,
    Assumption,
    Justification,
}

impl ElementKind {
    /// Every core element kind, in the order the standard introduces them.
    pub const ALL: [ElementKind; 6] = [
        ElementKind::Goal,
        ElementKind::Strategy,
        ElementKind::Solution,
        ElementKind::Context,
        ElementKind::Assumption,
        ElementKind::Justification,
    ];

    /// The letter a conventional GSN identifier starts with, as in `G1` or `Sn2`.
    pub fn identifier_prefix(self) -> &'static str {
        match self {
            ElementKind::Goal => "G",
            ElementKind::Strategy => "S",
            ElementKind::Solution => "Sn",
            ElementKind::Context => "C",
            ElementKind::Assumption => "A",
            ElementKind::Justification => "J",
        }
    }
}

#[cfg(test)]
mod tests {
    use super::ElementKind;
    use std::collections::HashSet;

    #[test]
    fn identifier_prefixes_are_distinct() {
        let prefixes: HashSet<&str> = ElementKind::ALL
            .iter()
            .map(|kind| kind.identifier_prefix())
            .collect();
        assert_eq!(prefixes.len(), ElementKind::ALL.len());
    }
}
