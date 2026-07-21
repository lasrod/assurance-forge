# Phase 9 Stage 3 — projection baseline

Stage 3 loads every repository fixture through both the legacy parser and the
SACM library, projects the library document into the same POD model the
application renders from, and compares. **The differences are the deliverable.**
Each one is either a projection bug to fix or a legacy behaviour to consciously
drop, and Stage 4 — making the library the source of truth — is gated on this
list reaching zero.

Nothing in the application depends on the library yet. The measurement comes
first precisely so that migrating does not silently change what users see.

Test: `tests/test_sacm_library_parallel_load.cpp`
Baseline: `tests/data/sacm_parallel_load_baseline.json`

The test fails on any *new* difference and on any baseline entry that no longer
occurs, so the list can only shrink and cannot be padded.

## Current state: 39 differences across 3 fixtures

Down from 98 once the terminology-expression shorthand was read (#201, fixed).
**Every remaining difference is a case where the library is correct and the
legacy parser is not** — there are no outstanding projection bugs. They clear
when Stage 4 makes the library the source of truth.

The baseline JSON (`tests/data/sacm_parallel_load_baseline.json`) keys by the
diff's coarse *category* — here always `field`, meaning a per-element field value
differs. The "which fields" column below is not stored in the baseline; it is the
human breakdown of what those `field` diffs actually are.

| Fixture | `field` (baseline count) | which fields |
|---|---:|---|
| `tests/data/fixture_roundtrip_sanitized_strict.sacm.xml` | 34 | `undeveloped` (all 34) |
| `tests/data/fixture_roundtrip_sample.sacm.xml` | 4 | `description` (all 4) |
| `tests/data/fixture_roundtrip_core_argument.sacm.xml` | 1 | `undeveloped` |

Total 39 `field` diffs = 35 `undeveloped` + 4 `description`.

## The causes

### 1. Terminology expressions dropped by the library — FIXED (#201)

This was the most important finding of Stage 3, and it is now closed.

Assurance Forge's own files write terminology contents with the concrete class
name as the element:

```xml
<expression id="TERM_SAFE" value="System operates without causing harm" />
```

instead of the canonical `<terminologyElement xsi:type="sacm:Expression">`. The
library treated `expression` as a reference role, so 59 expressions across four
fixtures were dropped entirely — a whole terminology package could vanish with
no error. The reader now recognizes the shorthand under a terminology container
in tolerant mode (strict still rejects it), test
`SACM23_TERM_001_LegacyTerminologyShorthandIsRead`. The count fell from 98 to 39.

### 2. `undeveloped` — the library reading is correct (6, decided)

The legacy parser reads only a separate GSN `undeveloped="true"` attribute and
**ignores `assertionDeclaration` entirely**. The affected fixtures already use
the SACM-native `assertionDeclaration="needsSupport"` with no boolean, so the
legacy parser reports them as developed while the projection — correctly —
reports them as undeveloped.

**Decided (2026-07-20): adopt the SACM reading.** GSN v3 and the ACWG
transformation map `undeveloped` to `assertionDeclaration = needsSupport`, so
`needsSupport` *is* the undeveloped state; the legacy dual representation was
redundant and could drift. The library now also normalizes a legacy
`undeveloped="true"` attribute onto `needsSupport` on tolerant read
(`SACM23_ARG_001_LegacyUndevelopedNormalizesToNeedsSupport`), so older files that
used the boolean keep working and no non-standard attribute is carried.

These six differences stay in the baseline because they are the *legacy parser*
being wrong, not the library — they resolve when the application switches to the
library (Stage 4), exactly like the description differences below. The visible
effect is that more goals correctly show the undeveloped diamond, which is the
intended alignment, not a regression.

### 3. The library reads descriptions the legacy parser misses (3)

Cases where legacy yields an empty description and the library yields real text,
e.g. `G2.description: legacy '' vs projected 'Hazards are identified'`.

Here **the library is right and the legacy parser is losing data.** These three
resolve themselves on migration; they are listed so the count reconciles and so
nobody "fixes" the projection to match a worse parser.

## Deliberate exclusions

Two things are excluded from the comparison rather than reported as differences,
because reporting them would be noise:

- **Packages** (`AssuranceCasePackage`, `ArgumentPackage`, `ArtifactPackage`).
  The POD model's `elements` are the nodes the application draws; packages are
  containers. The legacy parser lists neither.
- **Utility elements** (`Description`, `Note`, `TaggedValue`,
  `ImplementationConstraint`). These are metadata carried *on* elements, not
  elements in their own right.
- **Assurance Claim Points.** Synthesized by the application from vendor
  TaggedValues rather than read from SACM, so they are not part of this
  projection. See `docs/sacm/sacm-gsn-metamodel-gaps.md`.

Excluding these took the count from roughly 400 to 98, and the remainder are all
substantive.

## Stage 4 slice 1: full-field comparison

Stage 3 compared 7 fields; Stage 4 needs the projection proven equivalent across
*every* POD field before rendering depends on it, so `diff_cases` now compares
content, gid, assertion_declaration, reasoning_ref, meta_claim_refs, and the
name/description/content language maps too.

Closing the obvious gaps took the full-field count from ~2,500 to **419**:

- Language-map `"en"` defaulting (the legacy parser keys an untagged language as
  `en`) cleared ~800 `name_langs` and most `description_langs` differences.
- `assertion_declaration` is normalized empty-≡-`asserted` (the library makes the
  clause-11.10 default explicit; same meaning), clearing ~394.
- Term/Expression `value` now populates `content`.

The remaining 419 are two groups:

| Group | Count | Meaning |
|---|---:|---|
| **content / content_langs** | 376 | The goal *statement*. AF stores it in a `content=` attribute (a two-field model: content=statement, description=note); SACM stores the statement in the Description. **Decided: adopt the SACM model (statement = Description).** Implementing it needs the library reader to separate statement from note, so the statement's Description is primary — the next slice. Until then the projection does not surface it. |
| undeveloped / description(+langs) | 43 | The library is more correct than the legacy parser (needsSupport→undeveloped; descriptions legacy misses). Accepted; clear on migration. |

## What Stage 4 needs

The terminology-expression fix (#201) closed the only projection *bug*. The
remaining 39 differences are all cases where the library is correct and the
legacy parser is not — 35 `needsSupport` claims the legacy parser fails to mark
undeveloped, and 4 descriptions it misses. There is nothing left to *fix* in the
projection; these resolve when Stage 4 makes the library the source of truth and
the legacy parser is retired as the comparison oracle.

At that point the baseline reaches zero and `SACM23-INT-001` can move from
`implemented` toward `verified`.
