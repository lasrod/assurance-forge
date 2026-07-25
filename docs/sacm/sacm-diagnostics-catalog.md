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

## Identity and references

| Code | Severity | Meaning |
| --- | --- | --- |
| SACM-ID-001 | Error | Duplicate element ID. |
| SACM-ID-002 | Error | Invalid element ID syntax. |
| SACM-REF-001 | Error | Dangling reference: target ID does not resolve. |
| SACM-REF-002 | Error | Reference resolves to an element of the wrong type. |
| SACM-REF-003 | Warning | Reference resolves to an element the document carries only as preserved compatibility content, so it cannot be type-checked. Distinct from SACM-REF-001: the target is present in the source, it is merely untyped. Conflating them reports an intact argument as structurally broken. |

## Validation

| Code | Severity | Meaning |
| --- | --- | --- |
| SACM-ENUM-001 | Error | Invalid enumeration literal (e.g. `assertionDeclaration`). |
| SACM-MULT-001 | Error | Multiplicity violation (e.g. `AssertedRelationship` requires at least one source and one target). |
| SACM-CITE-001 | Error | `isCitation` is true but `citedElement` is absent (or vice-versa constraints per clause 8.2). |

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
