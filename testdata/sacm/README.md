# SACM sample cases

Assurance cases in SACM 2.3 XMI, for tests and for trying the application.

| File | Elements | What it is |
|---|---:|---|
| `sample.sacm.xmi` | 24 | A minimal case with a terminology package and an artifact package. |
| `sacm_example_core_argument.sacm.xmi` | 24 | A small braking argument with one argument package and nothing else. |
| `example.sacm.xmi` | 47 | The welcome page's example: a braking argument with every GSN element and mark the editor supports (each node kind, an undeveloped Goal, a counter goal of a line and counter evidence of a Goal, an ACP with a description and one with a confidence argument in a second package), with descriptions in English and Japanese. Written by hand on 2026-11-28; `crates/af-gsn/tests/example.rs` holds it to that. |
| `open-autonomy-safety-case.updated.v2.sacm.xmi` | 465 | The Open Autonomy Safety Case, in English. |
| `oasc-ja.sacm.xmi` | 581 | A variant of the same case, with English and Japanese text. |
| `safety-case-sanitized-strict.sacm.xmi` | 1,090 | The largest case: 15 argument packages. |
| `gallery-shared-support.sacm.xmi` | 31 | Layout gallery: a goal and evidence supporting more than one parent, at different depths. |
| `gallery-attachments.sacm.xmi` | 43 | Layout gallery: a goal and a strategy with several contexts, assumptions and justifications, and a context of two goals. |
| `gallery-long-text.sacm.xmi` | 23 | Layout gallery: long names and a long statement, identifiers with no break opportunity, a URL. |
| `gallery-japanese.sacm.xmi` | 23 | Layout gallery: statements in Japanese and English, Japanese mixed with Latin terms, one statement only in English. |
| `gallery-roots-and-cycle.sacm.xmi` | 25 | Layout gallery: two roots, a cycle of three goals, and a goal that supports itself. |
| `challenges.sacm.xmi` | 47 | Written by hand for the layout: context on both sides of a goal, and a challenge of a claim, of a context, of a line of support, of a line of context, of an inference with a Strategy and of another challenge. |

"Elements" counts every element with an id, descriptions included.

## Where they come from

The five `gallery-*` files were written on 2026-11-22 by `make_gallery.py`
(`python -I testdata/sacm/make_gallery.py testdata/sacm`) for the layout
gallery (`cargo run -p af-layout --example gallery`). They use only the
encodings of the reading rules in `docs/standards/sacm-gsn-mapping.md`, and
each reads strictly with no diagnostics. Change the script and run it again
rather than editing them by hand.

`challenges.sacm.xmi` was written by hand in SACM 2.3 XMI on 2026-11-21, since
no converted case has a counter relationship. The rest of this section is
about the other five.

They were written by the C++ application in its own dialect of SACM, and were
converted once, on 2026-11-15, with that application's library
(`sacm_cli export`, on the `cpp` branch). The originals are on the `cpp` branch
under the same names with the extension `.xml`.

The conversion applied the C++ library's recorded rules:

- An element's statement became its first description (SACM 2.3 clause 8.9).
- A name given in several languages kept the first as the name. The others are
  in a tagged value with the key `sacm.import.name`, because a SACM name holds
  one language (clause 8.6).

Three changes were then made to the text of each file, none of which changes
its content: `xsi:type` became `xmi:type`, and the `xmi:version` attribute and
the unused `xsi` namespace declaration were removed.

Two English-only variants of the Open Autonomy case were not kept. Their content
is the English text of `oasc-ja`.

## Known state

- **libSACM does not read the files with artifact or terminology packages yet**
  (lasrod/libSACM#66). `sacm_example_core_argument.sacm.xmi` loads today.
- **Many elements have two descriptions**, the statement and a note about it.
  SACM 2.3 allows one, and libSACM reports the second as a warning
  (SACM-MULT-001). Where the note should go is not decided.
