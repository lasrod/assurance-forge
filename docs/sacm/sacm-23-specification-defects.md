# SACM 2.3 specification defects found by implementation

Defects in the normative SACM 2.3 text (`formal/23-05-08`) and its
machine-readable model (`ptc/22-03-13`), found while implementing clause-level
validation in `libs/sacm`. Each was found because a check had to be written
against the clause, which is a different exercise from reading it.

This page is **input to the OMG submission**, not a description of Assurance
Forge. What the library does about each one is recorded in
[decisions and questions](sacm-decisions-and-questions.md); what it validates is
in the [conformance matrix](sacm-conformance-matrix.md). Prepared for
[#200](https://github.com/lasrod/assurance-forge/issues/200).

Every quotation is from the published PDF unless marked otherwise.

## D1 — Clause 10.5 prints clause 10.6's Constraints

`TerminologyPackageInterface` (10.5) and `TerminologyPackageBinding` (10.6) carry
**identical** Constraints sections:

> The participantPackages should be either TerminologyPackage or
> TerminologyPackageInterface
> OCL: `self.participantPackage->forall(pp|pp.oclIsKindOf(Terminology::TerminologyPackage))`

`participantPackage` is a *binding* association. An interface has no
participants — it has `implements`. So 10.5 states a constraint on a feature its
own class does not have, and the constraint the interface should carry is
absent.

**Why it matters.** Its siblings both state that constraint: 11.6 says an
`ArgumentPackageInterface` is "only allowed with isCitation=true and
+citedElement refer to ArgumentAssets within the ArgumentPackage implementation
referred to by implements", and 12.5 says the same for `ArtifactPackageInterface`.
A `TerminologyPackageInterface` is left with no content rule at all, so two
conforming tools can disagree about whether one may contain non-citations.

**Suggested resolution.** Replace 10.5's Constraints with the terminology
analogue of 11.6.

## D2 — Three clauses give three different participant-typing rules

The same concept — what may be a participant of a package binding — is specified
four times and agrees with itself nowhere:

| Clause | Rule as written |
|---|---|
| 9.4 `AssuranceCasePackageBinding` | `forall(pp \| pp.oclIsTypeOf(AssuranceCasePackage) or pp.oclIsTypeOf(AssuranceCasePackageInterface))` — exact type; **excludes** a binding |
| 10.6 `TerminologyPackageBinding` | `forall(pp \| pp.oclIsKindOf(Terminology::TerminologyPackage))` — kind-of; **admits** a binding |
| 11.5 `ArgumentPackageBinding` | `forall(pp \| pp.oclIsTypeOf(Argument::ArgumentPackageInterface))` — **interfaces only** |
| 11.5 Associations block | `participantPackage : ArgumentPackage[2..*]` — the general type |

11.5 contradicts **itself**: its OCL admits only `ArgumentPackageInterface` while
its own Associations block declares `ArgumentPackage`.

**Why it matters.** A validator cannot be written to satisfy all four. A document
whose `ArgumentPackageBinding` names two plain `ArgumentPackage`s is conformant
under the Associations block and non-conformant under the OCL three lines below
it. Assurance Forge enforces only what all four agree on — that a binding is not
a participant of a binding — at warning severity, and records the reason.

**Suggested resolution.** Pick one rule and state it identically in all four
clauses. `oclIsKindOf(<Family>Package)` excluding `<Family>PackageBinding` is the
reading that satisfies every clause's prose.

## D3 — Clause 11.5's OCL is not well-formed

> `self.argumentationElement->forAll(e|e.isCitation = true and e.citedElement <> null`

The expression is unbalanced — one closing parenthesis short — and the sentence
ends without a terminator. It is the only statement of the binding-content rule
in OCL form.

**Suggested resolution.** `self.argumentationElement->forAll(e | e.isCitation = true and e.citedElement <> null)`.

## D4 — Clause 11.4 and clause 11.6 cannot both be satisfied

11.4 Constraints:

> If an ArgumentPackage has nested ArgumentPackages, then it is only allowed to
> contain ArgumentPackages.

11.6 Semantics:

> An ArgumentPackageInterface resides insided the ArgumentPackage to which it
> refers.

An `ArgumentPackageInterface` **is** an `ArgumentPackage` (11.6 Superclass), and
11.6 requires it to reside inside the package it describes. So any package that
declares an interface "has nested ArgumentPackages", and 11.4 then forbids it
from containing the claims the interface exists to expose. Every non-empty
package with an interface is non-conformant by construction.

**Suggested resolution.** Scope 11.4 to nested packages that are neither
interfaces nor bindings, which is the reading that makes both clauses hold.
(Note the same sentence carries the typo "insided".)

## D5 — `Resource.location` is in the text and absent from the model

12.10 Attributes:

> `location:Base::MultiLangString` (composition) — the path or URL specifying the
> location of the Resource, can be in multiple languages.

`ptc/22-03-13` declares no such attribute on `Resource`. `location` is the only
payload the class has: without it a `Resource` records that a resource exists and
nothing about where.

**Why it matters.** A producer implementing the text emits `<location>`; a
consumer implementing the model has nowhere to put it. This is the most
consequential of the divergences here because it is not a spelling difference —
it is the entire content of a class.

**Suggested resolution.** Add it to the machine-readable model.

## D6 — Seven further prose/model divergences

Found by holding the text against `ptc/22-03-13` attribute by attribute. Listed
with the direction Assurance Forge resolved them and why, in
[the metamodel inventory](sacm-2.3-metamodel-inventory.md).

| Clause | Text | Model |
|---|---|---|
| 8.5 `MultiLangString.value` | `LangString[1..*]` composition | no bounds, i.e. `[1..1]` |
| 8.7 `UtilityElement.content` | `[0..1]` | `[1..1]` |
| 8.6 `ModelElement.description` | `Description[0..1]` | `[0..*]` |
| 8.4 `ExpressionLangString.expression` | "(composition)" marker with reference wording | plain reference |
| 11.2 / 11.4 member role | `argumentationElement` | `argumentElement` |
| 10.5 `TerminologyPackageInterface` superclass | "TerminologyElement" | `TerminologyPackage` (the clause's own diagram agrees with the model) |
| 12.10 `Resource.location` | present | absent (D5) |

8.5's is the one with a consequence beyond spelling: at `[1..1]` a
`MultiLangString` holds one language, which makes the clause's own uniqueness
constraint — "For each of the LangString in the value feature, their +lang must
be unique" — vacuous.

**Suggested resolution.** Regenerate the machine-readable model from the text, or
state which of the two is normative when they differ. Clause 2 requires importing
"XMI documents that conform with the SACM XML Schema produced by applying XMI
rules to the normative MOF metamodel", which points at the model; the rest of the
specification is the text. Implementers currently have to choose per attribute.

## D7 — No instance-document namespace is determined

The normative MOF model declares no `nsURI` on any package, and the PDF prints
none. Under XMI 2.5.1 an instance document's namespace derives from the
`org.omg.xmi.nsURI` tag, so nothing in SACM 2.3 determines one.

**Consequence, observed.** Every producer invents one and no two agree. The EMF
reference implementation ([github.com/wrwei/SACM](https://github.com/wrwei/SACM))
declares one namespace *per metamodel package* (`http://omg.sacm/2.2/base`,
`/argumentation`, …); this project pins a single document namespace. Both are
conformant, and neither can read the other without dialect-specific code. That is
the interoperability failure clause 2 exists to prevent.

**Suggested resolution.** Determine a namespace in 2.4, and say whether it is
per-document or per-package.

## Related

- [GSN / SACM metamodel gaps](sacm-gsn-metamodel-gaps.md) — the GSN v3 side, for the SCSC ACWG.
- [SACM 2.4 watch](sacm-2.4-watch.md) — the RTF issues that would change this.
- [Decisions and questions](sacm-decisions-and-questions.md) — what the library does about each defect.
