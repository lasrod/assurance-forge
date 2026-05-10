# Terminology Assist

Terminology Assist helps teams keep the vocabulary in an assurance case precise, reviewable, and connected to the SACM model. It supports SACM `TerminologyPackage` content directly, so terms, definitions, categories, and references remain part of the case data instead of living in a separate glossary file.

## Purpose

Safety cases often rely on compact terms and acronyms. A statement such as "The ODD is well defined" is only useful when reviewers know which definition of `ODD` is intended.

Terminology Assist helps by:

- Keeping controlled vocabulary in the assurance case.
- Detecting known terms in GSN element text.
- Flagging important-looking undefined acronyms.
- Showing term definitions from the GSN Canvas.
- Making duplicate acronyms safe by treating them as ambiguous until the intended meaning is chosen.
- Letting users promote important term usage into explicit argument context.

Inline term detection is reading and authoring assistance. It does not rewrite claim text and does not create argument semantics unless the user chooses **Add as Context**.

## SACM Storage Model

Assurance Forge stores glossary data using SACM terminology concepts.

| User concept | SACM storage |
| --- | --- |
| Glossary | `TerminologyPackage` |
| Term text matched in argument text | `Term.value` |
| Full name or display name | `Term.name` |
| Definition | inherited `description` on `Term` |
| Source URI or standard reference | `Term.externalReference` |
| Originating model element or source note | `Term.origin` |
| Category | SACM `Category` referenced by the term |

Assurance Forge does not add a custom `definition` field. Definitions are stored in the inherited SACM `description` field.

When a term is added as explicit context, Assurance Forge stores that argument relationship as SACM argument structure:

- An `ArtifactReference` points to the selected `Term`.
- An `AssertedContext` connects that `ArtifactReference` to the selected claim or strategy.
- The context appears as a context-style node in the GSN Canvas.

## Creating a Glossary

Terminology packages appear in the project file/package view as first-class SACM packages. Open a SACM file, expand its package structure, and select a `TerminologyPackage` to open the terminology editor.

A terminology package has:

- A package name.
- An optional package description.
- A table of terms.
- Categories that can be assigned to terms.

Use categories to keep larger glossaries navigable. Typical categories include operational context, system, hazard or risk, evidence, requirement, standard, and project-specific terminology.

## Defining a Term

Open a terminology package and create a term from the glossary table. A useful term entry usually includes the visible term text, a full name, a definition, and a source reference.

Example:

| Field | Value |
| --- | --- |
| Term | `ODD` |
| Full Name / Display Name | Operational Design Domain |
| Definition | The operating conditions under which the system is intended to function. |
| Category | Operational Context |
| External Reference | ISO 34503 or the project reference that defines the term |

After the term is created, matching text in GSN element content can be detected and shown as a glossary term.

## Defining a Term While Writing a Goal

When visible GSN text contains an important-looking undefined acronym, Assurance Forge can suggest terminology actions. For example, if a goal says:

```text
The ODD is well defined.
```

and `ODD` is not defined in the active terminology scope, the workflow can offer actions such as:

- **Define term**: create a new SACM `Term` without leaving the authoring flow.
- **Link existing term**: choose an existing glossary entry when the acronym is already defined elsewhere.
- **Ignore**: suppress the suggestion for the current session when the text is not a glossary term.

Quick define creates a normal SACM term in the selected terminology package. The goal text remains unchanged.

## Reading Clickable Terms in the GSN Canvas

When the GSN Canvas sees known terminology in visible element text, it can show the term as interactive text. Opening the term card gives quick access to the glossary entry.

A term card can show:

- Term value.
- Full name.
- Definition.
- Categories.
- External reference.
- Origin.
- Usage actions.

Common actions include opening the full term, editing the term, finding usages, and adding the term as explicit context.

## Handling Ambiguous Terms

Duplicate visible term values are allowed. In safety and autonomy work, the same acronym can legitimately mean different things.

For example, one terminology package might contain:

| Term | Full name |
| --- | --- |
| `ODD` | Operational Design Domain |
| `ODD` | Object Detection Dataset |

When a claim says "The ODD is well defined," Assurance Forge must not silently choose one meaning. The occurrence is ambiguous until the user chooses the intended term or attaches an explicit context.

Ambiguous term usage can appear in the GSN Canvas and in the Problems panel. Resolving the ambiguity records which glossary meaning should be used for that element or occurrence, depending on the available action.

A duplicate term value is not a validation warning by itself. A warning is appropriate when the same term value has the same non-empty definition, because that may indicate accidental duplicate glossary entries.

## Adding a Term as Explicit Context

Normal glossary lookup and **Add as Context** are different workflows.

| Workflow | Meaning |
| --- | --- |
| Glossary lookup | Helps readers understand text. It is UI assistance and does not change the argument structure. |
| Add as Context | Creates an explicit SACM argument context relationship from the term to the selected claim or strategy. |

Use **Add as Context** when the term definition is important to the argument itself. For example, if a claim depends on the exact scope of `ODD`, adding `ODD` as context makes that dependency visible in the argument.

After **Add as Context**, the GSN Canvas shows a context-style node connected to the claim or strategy. The context references the original term, so updating the term definition updates the projected context display without duplicating the definition into the argument text.

## Finding Term Usages

Use **Find usages** from a glossary row or term card to see where a term appears in the current model.

Usage results can include:

- Goals or claims.
- Strategies.
- Contexts, assumptions, and justifications.
- Solutions or artifact references.
- Explicit term contexts.

Each usage result shows the element, package, snippet, and resolution status. Double-click or use the navigation action to move to the relevant GSN element or editor.

Resolution statuses help distinguish normal matches from cases that need review:

| Status | Meaning |
| --- | --- |
| Resolved | The term usage maps to the selected glossary entry. |
| Ambiguous | More than one visible term has the same value. |
| Explicit context | The term is attached as explicit argument context. |
| Undefined | The text looks like an important acronym but no term is defined. |

## Validation Rules

Terminology validation feeds the Problems panel so terminology issues can be reviewed with the rest of the case.

| Severity | Problem |
| --- | --- |
| Error | A term has no value. |
| Error | A visible context `ArtifactReference` points to a missing `Term`. |
| Error | A visible context source or target cannot be resolved. |
| Warning | A concrete term has no description. |
| Warning | A text occurrence is ambiguous. |
| Warning | An important-looking acronym is undefined. |
| Warning | A visible term context duplicates another visible context for the same target and term. |
| Warning | The same term value has the same non-empty definition in duplicate term entries. |
| Info | A term has no category. |
| Info | A term has no external reference or origin/source. |

Some Problems panel entries include quick fixes, such as defining an undefined term, opening ambiguity resolution, editing a term, or opening duplicate definitions.

Deprecated-term validation is not listed as an active rule unless the model includes a deprecated status for terms.

## Best Practices

Define terms close to the vocabulary used in claims, strategies, contexts, and evidence references.

Prefer short visible term values and clear full names. For example, use `ODD` as the visible value and "Operational Design Domain" as the full name.

Write definitions that are specific enough for review. If a term comes from a standard, project glossary, hazard analysis, or design document, record that source in the external reference or origin field.

Use categories to make large glossaries easier to scan.

Allow duplicate acronyms when they represent different concepts, but resolve ambiguous occurrences near important claims.

Use **Add as Context** only when the term definition is part of the argument semantics. Normal glossary lookup is better for lightweight reading assistance.

Review terminology Problems before release or external review. Undefined acronyms, ambiguous usage, and missing definitions are often early signs that a safety case will be hard to audit.
