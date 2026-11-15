# SACM sample cases

Assurance cases in SACM 2.3 XMI, for tests and for trying the application.

| File | Elements | What it is |
|---|---:|---|
| `sample.sacm.xmi` | 24 | A minimal case with a terminology package and an artifact package. |
| `sacm_example_core_argument.sacm.xmi` | 24 | A small braking argument with one argument package and nothing else. |
| `open-autonomy-safety-case.updated.v2.sacm.xmi` | 465 | The Open Autonomy Safety Case, in English. |
| `oasc-ja.sacm.xmi` | 581 | A variant of the same case, with English and Japanese text. |
| `safety-case-sanitized-strict.sacm.xmi` | 1,090 | The largest case: 15 argument packages. |

"Elements" counts every element with an id, descriptions included.

## Where they come from

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
