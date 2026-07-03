# SACM 2.3 conformance matrix

Status values:

- `not-started`
- `analyzed`
- `tests-failing`
- `implemented`
- `verified`
- `deferred`
- `out-of-scope`

This matrix is a working control artifact. The `sacm-spec-analyst` and `sacm-metamodel-cartographer` agents should refine source clauses and requirements before implementation.

The project goal is full SACM 2.3 compliance. Incremental rows may start with a subset, but subset status must not be represented as full compliance until verified.

| ID | Area | Source | Requirement | Type | Status | Library files | Tests | Notes |
|---|---|---|---|---|---|---|---|---|
| SACM23-LIB-001 | Library boundary | Architecture decision | The SACM library public API must not depend on Assurance Forge UI/app/core/parser/AI/review classes or names. | architecture | not-started | TBD | TBD | Enforce via CMake and include checks. |
| SACM23-LIB-002 | Source of truth | Architecture decision | Loaded SACM data must be owned by the SACM library; Assurance Forge projections must not become the serialization source of truth. | architecture | not-started | TBD | TBD | Adapter tests required. |
| SACM23-LIB-003 | Layout boundary | Architecture/layout policy | Layout, visual representation, canvas coordinates, tree positions, and GSN display state must not appear in the SACM library API or strict XMI output. | architecture | not-started | TBD | TBD | Assurance Forge may compute deterministic layout externally. |
| SACM23-CMD-001 | Editing API | Editing policy | The library must expose SACM-native mutation operations with structured mutation results and no GSN/UI terminology. | model/edit | not-started | TBD | TBD | First editable slice. |
| SACM23-CMD-002 | Create document/package | SACM package clauses/metamodel | The library must create a new SACM 2.3 document containing an `AssuranceCasePackage` with valid identity and strict-save behavior. | model/edit/xmi | not-started | TBD | TBD | First editable slice. |
| SACM23-CMD-003 | Create argument/claim | SACM argumentation clauses/metamodel | The library must create an `ArgumentPackage` and `Claim` using SACM terminology and preserve them through save/load. | model/edit/xmi | not-started | TBD | TBD | Maps to Assurance Forge Goal only in adapter. |
| SACM23-CMD-004 | Delete preview | Editing policy | Destructive delete operations must provide an operation preview with affected elements, relationships, diagnostics, and applicability before mutation. | edit/validation | not-started | TBD | TBD | Used by Assurance Forge confirmation UI. |
| SACM23-CMD-005 | Delete apply | Editing policy | Delete operations must apply only with explicit policy choices and must leave the document valid or unchanged on failure. | edit/validation | not-started | TBD | TBD | Include claim and package deletion first. |
| SACM23-CMD-006 | Mutation audit data | Editing/audit policy | Mutation results must expose created/changed/deleted IDs and enough metadata to support future undo/redo and Assurance Forge audit alignment. | edit/audit | not-started | TBD | TBD | Exact undo mechanism remains open. |
| SACM23-XMI-001 | XMI/root | SACM 2.3 normative PDF/XML | Strict SACM 2.3 import/export must recognize the standard document structure, namespaces, IDs, and top-level assurance case package behavior. | xmi | not-started | TBD | TBD | First slice. Confirm exact root serialization from normative artifacts. |
| SACM23-XMI-002 | XMI/namespaces | SACM 2.3 normative XML | Import must be namespace-prefix independent and export must be deterministic. | xmi | not-started | TBD | TBD | Include different prefix fixtures. |
| SACM23-XMI-003 | XMI/references | SACM 2.3 normative XML + XMI rules | References must preserve target identity and produce diagnostics for broken or mistyped references. | xmi | not-started | TBD | TBD | Required before complex relationships. |
| SACM23-XMI-004 | Strict save mode | Compliance policy | Strict save mode must emit SACM 2.3 XMI without Assurance Forge layout metadata or compatibility-only extensions. | xmi | not-started | TBD | TBD | Compatibility mode must be separate. |
| SACM23-RT-001 | Round trip | Compliance policy | Import -> export -> import must preserve all SACM semantics covered by the implemented slice. | test | not-started | TBD | TBD | Use semantic comparison, not raw text. |
| SACM23-RT-002 | Created document round trip | Editing policy | A document created through the library edit API must save, reload, validate, and semantically match the pre-save model for the covered slice. | test/edit/xmi | not-started | TBD | TBD | First editable slice. |
| SACM23-VAL-001 | Validation | Compliance policy | Parser and validator must report structured diagnostics with severity, requirement ID, location where practical, and message. | validation | not-started | TBD | TBD | Invalid files must not crash or silently repair. |
| SACM23-VAL-002 | Post-mutation validity | Editing policy | Public mutation operations must leave the document valid for the supported slice or fail unchanged. | validation/edit | not-started | TBD | TBD | Test create/delete operations. |
| SACM23-BASE-001 | Base model | SACM base clauses/metamodel | Common SACM element identity, naming, descriptions, notes, language strings, and metadata must be represented and round-tripped. | model | not-started | TBD | TBD | Refine from formal clauses. |
| SACM23-BASE-002 | Base model | SACM base clauses/metamodel | Abstract/citation/implementation-constraint behavior must be validated according to the standard. | validation | not-started | TBD | TBD | Negative tests needed. |
| SACM23-PKG-001 | Assurance case package | SACM assurance case package clauses/metamodel | `AssuranceCasePackage` must be represented as the full interchange package and support contained terminology, argumentation, artifact, and nested packages as required. | model/xmi/edit | not-started | TBD | TBD | Minimal create/load/save first, complete later. |
| SACM23-PKG-002 | Package interfaces/bindings | SACM assurance case package clauses/metamodel | Package interfaces and bindings must preserve participants, references, multiplicities, and validation behavior. | model/validation | not-started | TBD | TBD | Important for modular assurance cases. |
| SACM23-PKG-003 | Package deletion | Editing policy + SACM containment rules | Package deletion must handle non-empty packages, recursive deletion, cross-package references, and preview/apply semantics explicitly. | edit/validation | not-started | TBD | TBD | Exact default policy needs investigation. |
| SACM23-TERM-001 | Terminology | SACM terminology clauses/metamodel | Terminology packages, groups, terms, categories, expressions, and external references must be represented and round-tripped. | model/xmi | not-started | TBD | TBD | Can be implemented independent of UI rendering. |
| SACM23-ARG-001 | Argumentation | SACM argumentation clauses/metamodel | Argument packages, claims, reasoning, artifact references, assertion declarations, and asserted relationships must be represented and round-tripped. | model/xmi/edit | not-started | TBD | TBD | GSN projection is a client concern. |
| SACM23-ARG-002 | Relationship typing | SACM argumentation clauses/metamodel | Asserted relationship source/target typing and multiplicities must be validated. | validation | not-started | TBD | TBD | Negative tests are essential. |
| SACM23-ARG-003 | Relationship edit consequences | Editing policy | Claim/package deletion must report and handle affected asserted relationships without leaving dangling references. | edit/validation | not-started | TBD | TBD | Preview first, explicit apply policy. |
| SACM23-ART-001 | Artifact model | SACM artifact clauses/metamodel | Artifact packages, artifacts, artifact assets, properties, events, resources, activities, techniques, participants, and relationships must be represented and round-tripped. | model/xmi | not-started | TBD | TBD | Evidence UI can lag model support. |
| SACM23-COMPAT-001 | Compatibility | Interoperability policy | Legacy SACM versions, third-party XMI variations, and vendor extensions must be explicit compatibility modes, not default strict SACM 2.3 behavior. | compatibility | not-started | TBD | TBD | Document import/export separately. |
| SACM23-SEC-001 | XML safety | Security policy | XML parser must disable unsafe features such as external entity expansion and report malformed XML safely. | security | not-started | TBD | TBD | Add XXE-style negative fixture if parser supports entities. |
| SACM23-INT-001 | Assurance Forge adapter | Integration plan | Assurance Forge must load, project, edit, and save through the library-owned document. | integration | not-started | TBD | TBD | Adapter tests prove hidden data is preserved. |
| SACM23-INT-002 | Delete confirmation integration | Integration plan | Assurance Forge delete UI should use library operation previews to show implications before applying deletes. | integration | not-started | TBD | TBD | Aligns current Assurance Forge behavior with library effects. |
| SACM23-CLI-001 | CLI utility | API decision | The repository should include a small CLI test utility for version, validate, import/export, and round-trip smoke workflows. | tooling | not-started | TBD | TBD | Start simple; expand as library matures. |
