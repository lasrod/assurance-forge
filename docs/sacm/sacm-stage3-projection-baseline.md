# Phase 9 Stage 3 — projection baseline

Stage 3 loads every repository fixture through both the legacy parser and the
SACM library, projects the library document into the same POD model the
application renders from, and compares. **The differences are the deliverable.**
Each one is either a projection bug to fix or a legacy behaviour to consciously
drop, and Stage 4 — making the library the source of truth — is gated on this
list reaching zero.

Nothing in the application depends on the library yet. The measurement comes
first precisely so that migrating does not silently change what users see.

## What this corpus can and cannot show

The comparison is **library projection vs legacy parser**, over six Assurance
Forge files. That bounds it twice over:

1. The reference side is a GSN-shaped reader that cannot express most of
   SACM 2.3, so pointing this test at richer files would produce a flood of
   legacy-parser gaps rather than projection findings.
2. The six files contain no `ArgumentGroup`, no `AssertedArtifactSupport` or
   `AssertedArtifactContext`, and no `TerminologyGroup`.

So "field-complete and lossless" is a statement **about this corpus**. It was
written on the SACM23-INT-001 row without that qualifier, and the 2026-07-26
round-2 verification of SACM23-LIB-002 falsified the general reading by
measurement -- a bridged edit deleted exactly the constructs this corpus lacks.

Element completeness beyond the corpus is now measured separately, against the
document rather than against the legacy parser:
`ProjectionCoverage.SACM23_INT_001_ProjectionEmitsEveryNonContainerElement`
sweeps `libs/sacm/tests/data/sacm23/*-valid.sacm.xmi` and requires `project_case`
to emit every element the library read, allowing only packages and clause-8.7
utility elements to be omitted.

**Field** completeness is still corpus-bound. Closing that needs a reference that
is not the legacy parser -- comparing the projection's fields against the library
model directly, rather than against a reader which cannot represent them.

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
- **Assurance Claim Points are now synthesized** by the projection from the same
  `assuranceForge.acp` vendor TaggedValues the app uses, matching the legacy
  parser's `extract_acps`. No repo fixture carries ACPs, so a dedicated fixture
  (`tests/data/fixture_acp_parity.sacm.xml`) exercises the synthesis in
  `SACM23_INT_001_ProjectionSynthesizesAcpsLikeLegacy`. ACP support was a hard
  requirement of the migration.

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

Adopting the SACM "statement = Description" model (clause 8.9) then took it to
**377**, and — more importantly — changed the *composition* so the projection no
longer loses anything:

- The reader now treats a legacy `content=`/`<content>` statement as the primary
  Description (front), so `description()` returns the statement.
- The projection surfaces that statement in the POD `content` field, but only for
  claim-like kinds (Claim, ArgumentReasoning) — artifacts, references and
  relationships legitimately carry a `<description>` that is a note, not a
  statement, and keep `content` empty as before.

Result: files that stored the statement in a `content=` attribute now **match**
the legacy parser exactly, and the projection always carries the statement. The
remaining 377 are all cases where the library is *more correct* than the legacy
parser, not losses:

| Group | Count | Meaning |
|---|---:|---|
| content / content_langs | ~334 | The projection surfaces the goal statement in `content` for canonical claims (statement in `<description>`, no `content=`), which the legacy parser left empty. The GSN node label uses `name`, so this is the inspector/statement view gaining the text, not a node change. |
| undeveloped / description(+langs) | 43 | needsSupport→undeveloped; descriptions the legacy parser misses. |

There are **no remaining projection losses** — every difference is the library
being at least as complete as the legacy parser. That is the property the render
flip (slice 2) requires.

## What Stage 4 needs

The terminology-expression fix (#201) closed the only projection *bug*. The
remaining 39 differences are all cases where the library is correct and the
legacy parser is not — 35 `needsSupport` claims the legacy parser fails to mark
undeveloped, and 4 descriptions it misses. There is nothing left to *fix* in the
projection; these resolve when Stage 4 makes the library the source of truth and
the legacy parser is retired as the comparison oracle.

At that point the baseline reaches zero and `SACM23-INT-001` can move from
`implemented` toward `verified`.
