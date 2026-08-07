# SACM AI prompts

**Historical.** These are the prompts the SACM library implementation was driven
with, kept for the reasoning they record. They are not current instruction, and
the agent-pack workflow they belong to is superseded by
[#294](https://github.com/lasrod/assurance-forge/issues/294).

Used from the repository root after installing the agent pack, in this order:

1. [Library master prompt](sacm-library-master-prompt.md) — the overall brief.
2. [First editable slice prompt](sacm-first-editable-slice-prompt.md) — the first
   vertical slice, editable rather than read-only.
3. [Verification prompt](sacm-verification-prompt.md) — drives the
   `sacm-conformance-verifier` agent that produces the
   [verification records](../verification/README.md).
4. [Assurance Forge adapter prompt](assurance-forge-adapter-prompt.md) — connects
   the application to the library.

Also here:

- [First slice prompt](sacm-first-slice-prompt.md) — the earlier read-only
  framing, superseded by the editable slice above.

`legacy-load-roundtrip-only-prompt.md` was deliberately never written. The
project decision was to start with a small editable vertical slice rather than a
read-only parser, so that editing was designed in rather than bolted on — see
the [editing policy](../sacm-editing-policy.md).
