# SACM 2.3 test strategy

## Principle

Test the reusable library at the SACM model/edit/XMI boundary first. Assurance Forge UI tests are secondary and should verify projection from the library, not redefine compliance.

The first real slice is editable: create package, create claim, preview delete, apply delete, validate, save, load, and semantic round-trip.

## Test layers

### 1. Library unit tests

Validate model invariants, IDs, references, validation helpers, command behavior, mutation previews, and small parser/serializer behavior.

### 2. Library edit tests

Validate SACM-native edit operations independently of Assurance Forge UI:

```text
create document
create AssuranceCasePackage
create ArgumentPackage
create Claim
preview delete Claim
apply delete Claim
preview delete package
apply delete package
```

Every public edit should leave the document valid for the supported slice or fail unchanged.

### 3. Library XMI fixture tests

Validate import/export/round-trip against SACM 2.3 fixtures.

### 4. Library semantic equivalence tests

Validate that import -> export -> import preserves semantics. Raw XML equality is only appropriate for deterministic golden output tests.

### 5. Negative validation tests

Validate structured diagnostics for malformed XML, broken references, wrong types, invalid enumerations, missing required data, unsupported strict behavior, or rejected destructive edits.

### 6. Assurance Forge adapter tests

Validate that the application projects library-owned data correctly, maps UI/GSN commands to SACM commands, uses delete previews, and does not lose UI-hidden SACM data.

### 7. Interoperability corpus tests

Validate Papyrus-style SACM material, OASC if available and usable, and minimized reproductions from other tools when licensing allows.

### 8. CLI utility tests

Validate the CLI can report version, validate a fixture, perform import/export, and run a round-trip smoke check for covered slices.

## Fixture layout

Preferred once library exists:

```text
libs/sacm/tests/data/sacm23/
libs/sacm/tests/data/sacm23/invalid/
libs/sacm/tests/data/interop/<tool-or-source>/
```

Temporary top-level layout is acceptable before the library has its own test target:

```text
tests/data/sacm23/
tests/data/sacm23/invalid/
```

## Naming convention

Fixtures:

```text
<clause-or-area>-<feature>-valid.sacm.xmi
<clause-or-area>-<feature>-invalid-<reason>.sacm.xmi
```

Tests:

```cpp
TEST(Sacm23Commands, SACM23_CMD_003_PreviewsDeleteClaimEffects)
TEST(Sacm23Commands, SACM23_CMD_005_DeleteClaimLeavesDocumentValid)
TEST(Sacm23XmiConformance, SACM23_XMI_001_ImportsMinimalAssuranceCasePackage)
TEST(Sacm23RoundTrip, SACM23_RT_002_CreatedDocumentSavesReloadsAndMatches)
TEST(Sacm23Validation, SACM23_XMI_003_ReportsBrokenReference)
```

## First slice test list

The first editable slice should include tests equivalent to:

```text
SACM23_LIB_001_PublicHeadersDoNotIncludeAssuranceForgeHeaders
SACM23_LIB_003_PublicApiDoesNotExposeLayoutOrGoalTerminology
SACM23_CMD_002_CreatesDocumentWithAssuranceCasePackage
SACM23_CMD_003_CreatesArgumentPackageAndClaim
SACM23_CMD_004_PreviewsClaimDelete
SACM23_CMD_005_AppliesClaimDeleteWithExplicitPolicy
SACM23_CMD_005_RejectsDeleteWhenPolicyDisallowsAffectedReferences
SACM23_CMD_004_PreviewsPackageDeleteConsequences
SACM23_VAL_002_MutationsLeaveDocumentValidOrUnchanged
SACM23_XMI_001_SavesStrictSACM23ForCreatedDocument
SACM23_RT_002_CreatedDocumentSavesReloadsAndSemanticallyMatches
SACM23_CLI_001_CliReportsVersionAndValidatesFixture
```

## Semantic comparison checklist

A semantic comparison should check at least:

- Element type.
- Element ID and global identity.
- Containment structure.
- Attribute values and default handling.
- Reference target IDs.
- Language tags and text values.
- Multiplicity/order where order is semantic or required for deterministic export.
- Extension preservation policy.
- Diagnostics if the document is intentionally partially supported.

## Operation preview checklist

A delete preview test should check:

- Requested target exists.
- Referencing relationships are listed.
- Contained children are listed for package deletes.
- Cross-package effects are listed or rejected according to policy.
- The preview states whether apply is possible.
- Applying the preview either succeeds and leaves the document valid or fails unchanged.

## Negative test examples

- Duplicate IDs.
- Broken references.
- Reference to wrong element type.
- Invalid enum literal.
- Missing required package/root structure.
- Unsupported standard element in strict mode when it cannot be preserved safely.
- Vendor extension rejected in strict mode and preserved/warned in compatibility mode, if extension policy allows it.
- External XML entity rejected or safely ignored.
- Compatibility-mode-only input rejected by strict mode.
- Delete rejected because references exist and policy is `RejectIfReferenced`.
- Delete rejected because package is non-empty and policy is `RejectIfNonEmpty`.

## Done criteria

A slice is test-complete only when:

- Requirement IDs are present in test names or assertion messages.
- At least one positive fixture proves successful behavior.
- At least one negative fixture proves diagnostics where applicable.
- Edit operations are tested where the slice includes editability.
- Round-trip semantic equivalence is tested.
- Golden export is tested when deterministic output shape is part of the requirement.
- Tests run without Assurance Forge UI/app dependencies unless the slice is explicitly an adapter slice.
