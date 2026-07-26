# GSN notation reference

Semantics first, shapes second. Where a symbol's exact rendering matters (a
pixel-level drawing task), confirm against GSN Community Standard v3
(SCSC-141C) — this file is a working reference, not a substitute for the
standard.

## Core elements

| Element | Shape | Means | Written as |
|---|---|---|---|
| **Goal** | Rectangle | A claim that is asserted to be true. The unit of argument. | A proposition that can be true or false. "All identified hazards have been mitigated to ALARP" — never a topic like "Hazard analysis". |
| **Strategy** | Parallelogram | The reasoning step from a goal to its sub-goals. Explains *why* the sub-goals together establish the parent. | "Argument over each identified hazard" |
| **Solution** | Circle | A reference to evidence that discharges a goal. | A reference to an artifact, not a claim about it. |
| **Context** | Rounded rectangle (stadium) | The scope, definitions, or referenced material within which a goal or strategy is to be interpreted. | "Hazards as defined in Ref X" |
| **Assumption** | Oval, annotated `A` | A proposition taken as true without support, whose falsity would undermine the argument. | Must be a proposition. |
| **Justification** | Oval, annotated `J` | Rationale for why a goal or strategy is *appropriate*. | About the argument, not about the system. |

Every element carries an **identifier** (G1, S1, Sn1, C1, A1, J1 by convention).
GSN v3 makes the identifier mandatory.

**Assumption vs Justification** is the distinction most often got wrong. An
Assumption asserts something about the world that the argument depends on and
does not prove. A Justification explains why this particular goal or strategy is
a reasonable thing to do. If it could be false and break the argument, it is an
Assumption; if it is a rationale for a modelling choice, it is a Justification.

**Context vs Assumption** is the second. Context scopes or defines; it does not
assert something contingent that the argument leans on. If the argument breaks
when the statement turns out false, it is an Assumption.

## Core relationships

| Relationship | Line | Arrowhead | From → To |
|---|---|---|---|
| **SupportedBy** | Solid | Solid filled | Goal or Strategy → the element that supports it |
| **InContextOf** | Solid | Hollow / open | Goal or Strategy → Context, Assumption or Justification |

Both run **conclusion → premise** in GSN. SACM's `AssertedInference` and
`AssertedContext` run the other way; see `docs/sacm/sacm-gsn-mapping.md`.

`SupportedBy` connects Goal/Strategy to Goal, Strategy or Solution.
`InContextOf` connects Goal/Strategy to Context, Assumption or Justification.
A Solution is always a leaf.

## Core decorators

| Decorator | Symbol | Means |
|---|---|---|
| **Undeveloped** | Hollow diamond, bottom-centre | The element requires support that has not yet been provided. Legitimate and intentional — an undeveloped goal is an honest statement of incompleteness, not a defect. |

An undecorated leaf Goal is a defect: it claims something with no support and no
acknowledgement that support is missing.

## Pattern Extension

For argument *patterns* — reusable, abstract argument structures instantiated
against a specific system.

| Construct | Means |
|---|---|
| **Uninstantiated** | The element is abstract; it contains placeholders (`{hazard}`) that must be bound before the argument is concrete. Maps to SACM `isAbstract`. |
| **Undeveloped and uninstantiated** | Both at once: abstract *and* unsupported. |
| **Multiplicity** | The relationship is repeated — "one supporting goal per hazard". Cardinality given as a label. GSN `isMany`; **no SACM 2.3 feature carries this**. |
| **Optionality** | The relationship may or may not be present. GSN `isOptional`; **no SACM 2.3 feature carries this**. |
| **Choice** | m-of-n selection among supporting elements. The `Choice` class in metamodel v2.2 has *no attributes* — it exists only to let one relationship fan out. The "m of n" cardinality is a GSN v3 concept with no metamodel. SACM 2.4 draft `Join.lowerBound`/`upperBound` is the intended landing place; align with it rather than inventing an encoding. |
| **Pattern definition** | v3 §1:3.4. No GSN or SACM class exists. |

## Modular Extension

For arguments split across modules with defined interfaces.

| Construct | Means | SACM |
|---|---|---|
| **Module** | A self-contained argument fragment. | `ArgumentPackage` |
| **Contract module** | Binds two modules: what one offers, what the other relies on. | `ArgumentPackageBinding` — **not** `ArgumentPackage` |
| **Away Goal / Solution / Context / Assumption / Justification** | An element defined in *another* module, cited here. Drawn with the source module's identifier in a bottom compartment. | Away{Goal,Assumption,Justification} → `Claim` with `isCitation`/`citedElement`; AwaySolution → `ArtifactReference`; **AwayContext is contested and is preserved rather than retyped** |
| **Module reference / contract reference** | A pointer to a module as a diagram element. | `ArtifactReference` — "modules are modelled as Artifacts" |
| **Module interface** | The public surface of a module. | `ArgumentPackageInterface` exists in SACM but v2.2 OCL disables it. v3 §1:4.6 is normative; this is a metamodel defect. |
| **Public** decorator | The element is part of the module's public interface. | TaggedValue |
| **To-be-supported-by-contract** decorator | Support will come via a contract, not from within this module. | `needsSupport` + TaggedValue |
| **Architecture view** | Module-level diagram showing modules and their dependencies. | Diagrammatic; lands in the SACM 2.4 draft. |

## Confidence / Assurance Claim Points (v3)

An **Assurance Claim Point (ACP)** marks a place in the argument where a
*confidence argument* attaches — an argument about how much you should trust
that part of the argument, kept separate so it does not clutter the primary one.

- Placed on a relationship (`SupportedBy`, `InContextOf`) or on an element.
- Carries a **unique identifier**; module-qualified as `ACP1[Confidence]` when
  the confidence argument lives in another module (v3 §1:5.2.3).
- v3 §1:5.2.2 says ACPs "may also be added to any element of an argument that
  provides a reference to an artefact e.g. solution or context" — **and SACM 2.3
  structurally cannot carry that**, because `ArtifactReference` and
  `ArgumentAsset` are not `Assertion`s and so have no `metaClaim`. Open at OMG
  as SACM24-83.
- SACM 2.3's `Assertion.metaClaim` is the current carrier, and SACM24-38/-50
  remove it in 2.4; `Claim.subject` (SACM24-83) is the drafted replacement.

## Dialectic Extension (v3)

For recording *challenges* to an argument rather than only its support.

| Construct | Means | SACM |
|---|---|---|
| **Challenge** relationship | Dashed line, open arrowhead, pointing at what is challenged — an element *or* a relationship. A challenge may itself be challenged. | `AssertedRelationship.isCounter = true`. Metamodel v2.2 OCL forbids it (`self.isCounter = false`). |
| **Counter-evidence / counter-goal** | The source of a challenge: evidence or a claim that undermines rather than supports. | Same |
| **Defeated** decorator | The element or relationship is no longer valid because a challenge succeeded. | `AssertionDeclaration::defeated`. SACM24-12 proposes removing it. |
| **Defeated on a Strategy** | Normatively required by v3 §1:6.3.12 — challenging a multi-`SupportedBy` inference "requires a strategy to be inserted… the defeated decorator… is applied". | **No representation.** Strategy → `ArgumentReasoning`, not an `Assertion`. There is no attribute to set. |

Dialectics are the clearest case of rule 1: v2.2 makes them unrepresentable in
conformant GSN even though SACM 2.3 supports them. Assurance Forge implements
them anyway, and the gap is documented for the standards bodies.
