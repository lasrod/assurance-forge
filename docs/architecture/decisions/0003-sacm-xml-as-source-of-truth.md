# 0003. SACM XML as the source of truth

- Status: Accepted
- Date: 2026-06-17
- Deciders: Assurance Forge maintainers

## Context

Assurance Forge edits structured assurance and safety cases. Users' arguments
carry real safety significance, and the tool must be trustworthy: it must not
quietly alter, drop, or reinterpret the meaning of assurance data. Users also
need confidence that their data is portable and not locked into a proprietary
representation.

Internally the application maintains several model shapes for different
purposes — a flat parser model (`parser::AssuranceCase`) for the UI and review
logic, a typed SACM domain model (`sacm::AssuranceCasePackage`), and a derived
tree (`core::AssuranceTree`) for the tree view and GSN canvas. With multiple
models in play, we must be unambiguous about which one is authoritative.

## Decision

We will treat **SACM 2.3 XML** as the canonical representation of an assurance
case. Internal models are either projections of, or must be mirrored back into,
the SACM package before save.

Concretely:

- Parser-model changes must be mirrored into the SACM package before saving.
- `core::AssuranceTree` and the GSN canvas are *derived* data. They are rebuilt
  from the model after changes and are never mutated directly as a primary
  store.
- The tool must never silently modify or reinterpret a safety argument.
- Round-trip integrity (import → export) must be preserved for assurance-case
  data.

## Consequences

- Users can import and export SACM 2.3 XML with confidence that meaning is
  preserved, and there is no vendor lock-in.
- Changes to parsing, serialization, or model mutation require tests, and
  round-trip behavior must be validated whenever practical.
- Editing flows carry an obligation to keep the parser model and SACM package in
  sync, and to rebuild derived views rather than editing them in place.
- Features are constrained to what can be represented faithfully in SACM; the
  tool will not introduce state that cannot survive a round trip.
