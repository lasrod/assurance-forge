# SACM library diagnostics catalog

Machine-readable diagnostic codes emitted by the SACM 2.3 library
(`libs/sacm`). Codes are stable API: once released, a code's meaning must not
change. Constants live in `libs/sacm/include/sacm/validation/codes.h`.

Every `sacm::validation::Diagnostic` carries: `code`, `severity`
(Info/Warning/Error), `requirement_id` (conformance-matrix row), `operation`
(for command diagnostics), `affected` element IDs, optional `location`
(file/line/column), and a human-readable `message`.

## XML and security

| Code | Severity | Meaning |
| --- | --- | --- |
| SACM-XML-001 | Error | Malformed XML; parse failed. |
| SACM-SEC-001 | Error | Document contains a DOCTYPE/external entity declaration; rejected for safety. |

## XMI structure

| Code | Severity | Meaning |
| --- | --- | --- |
| SACM-XMI-001 | Error | Root element is not a recognized SACM interchange object (`AssuranceCasePackage`, `ArgumentPackage`, `ArtifactPackage`, `TerminologyPackage`, or an `xmi:XMI` wrapper of these). |
| SACM-XMI-002 | Error (strict) / Warning (tolerant) | Namespace URI is not a pinned SACM 2.3 namespace. |
| SACM-XMI-003 | Error (strict) / Warning (tolerant) | Unknown element; preserved in tolerant mode, rejected in strict mode. |
| SACM-XMI-004 | Error (strict) / Warning (tolerant) | Element lacks an `xmi:id`/`id`; generated in tolerant mode. |
| SACM-XMI-005 | Error (strict) | Containment role has an abstract declared type and the child carries no `xsi:type`. |
| SACM-XMI-006 | Error | Strict save refused: document contains preserved unknown content or compatibility-only data. |
| SACM-XMI-007 | Warning | External (cross-document) `href` reference is not supported; kept as opaque text in compatibility mode only. |
| SACM-XMI-008 | Warning | Document declares a SACM revision other than 2.3; loaded in compatibility mode. Distinct from SACM-XMI-002, which means the namespace itself is unrecognized. |
| SACM-XMI-009 | Warning | The document's root is not a SACM interchange package, but SACM packages were found inside it (an ODE `DDIPackage`, say). A tolerant load reads the SACM out and states that the file does not conform, that content outside the SACM packages is not represented, and that saving writes conformant SACM rather than the original container. Strict refuses the document (SACM-XMI-001). |

## Identity and references

| Code | Severity | Meaning |
| --- | --- | --- |
| SACM-ID-001 | Error | Duplicate element ID. |
| SACM-ID-002 | Error | Invalid element ID syntax. |
| SACM-ID-003 | Error | Duplicate `gid` (clause 8.2: "unique within the scope of the model instance"). Distinct from SACM-ID-001: `gid` is SACM's own model-global identifier rather than the XMI serialization id, and it is the handle third-party tools key on, so a document can be clean on one and broken on the other. An absent or empty `gid` is not an identity and is not compared. |
| SACM-REF-001 | Error | Dangling reference: target ID does not resolve. |
| SACM-REF-002 | Error | Reference resolves to an element of the wrong type. |
| SACM-REF-003 | Warning | Reference resolves to an element the document carries only as preserved compatibility content, so it cannot be type-checked. Distinct from SACM-REF-001: the target is present in the source, it is merely untyped. Conflating them reports an intact argument as structurally broken. |

## Validation

| Code | Severity | Meaning |
| --- | --- | --- |
| SACM-ENUM-001 | Error | Invalid enumeration literal (e.g. `assertionDeclaration`). |
| SACM-MULT-001 | Error | Multiplicity violation: `AssertedRelationship` requires at least one source (clause 11.13 `source[1..*]`) and exactly one target (`target[1]`); a binding requires at least two participant packages. |
| SACM-CITE-001 | Error | `isCitation` is true but `citedElement` is absent (or vice-versa constraints per clause 8.2). |
| SACM-ABS-001 | Error / Warning | An `isAbstract`/`abstractForm` combination the standard rules out. Error: an abstract element using `abstractForm` (clause 8.2 gives it to concrete elements), or a concrete `Expression` referencing abstract `ExpressionElement`s (clause 10.10 OCL). Warning: the referred `abstractForm` target is not abstract, or is a different type — clause 8.2 words both with "should". |
| SACM-EXPR-001 | Warning | An `ExpressionLangString` carries both an `expression` reference and literal `content`. Clause 8.4: "If expression is not empty, then +content should be empty." A warning because the clause says "should", but a real one: the two carry the same meaning twice and a reader has no rule for which wins. |
| SACM-PKG-001 | Error / Warning | Content the owning package clause does not allow. Error: an `ArgumentPackage` that nests an `ArgumentPackage` but contains something else (clause 11.4), interface or binding content that is not a citation (clauses 11.5, 11.6, 12.4, 12.5), or an `AssuranceCasePackageInterface` holding a non-interface sub-package (clause 9.3 OCL). Warning: an interface citation pointing outside the package it implements, and a binding named as another binding's participant — see the recorded resolutions in `sacm-decisions-and-questions.md`, since the parallel clauses disagree. |

## Commands

| Code | Severity | Meaning |
| --- | --- | --- |
| SACM-CMD-001 | Error | Operation target not found. |
| SACM-CMD-002 | Error | Delete rejected: element is referenced and policy is `RejectIfReferenced`. |
| SACM-CMD-003 | Error | Preview expired: the document changed after the preview was taken. |
| SACM-CMD-004 | Error | Create rejected: caller-provided ID already exists. |
| SACM-CMD-005 | Error | Invalid parent: the target cannot contain the requested child kind. |
| SACM-CMD-006 | Error | Delete rejected: package is not empty and policy is `RejectIfNonEmpty`. |
| SACM-CMD-007 | Error | Delete rejected: external (cross-package) references exist and policy is `RejectIfExternalReferencesExist`. |

Adding a code: append the row here, add the constant to
`validation/codes.h`, and reference the conformance-matrix requirement the
diagnostic supports.
