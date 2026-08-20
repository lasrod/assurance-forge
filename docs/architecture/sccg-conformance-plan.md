# SCCG tool-integration conformance — implementation plan

- **Status:** Planned.
- **Date:** 2026-08-19
- **Authority:** Reference until adopted; the ADRs it cites remain canonical on
  conflict.
- **Governing decision:**
  [ADR 0013](decisions/0013-review-methods-independent-of-inference-providers.md)
- **Depends on:**
  [Provider-neutral AI reviews and projectless MCP](ai-inference-and-projectless-mcp-plan.md),
  phases A1, A5 and C1.
- **Upstream contract:** the SCCG *Tool integration* page, published from the
  `external/safety-case-core-guidelines` submodule as `tool-integration.md` and
  the generated files under `dist/`.

---

## 1. Why this page exists, and what it is not

The sibling plan aligns the two review **paths**: it extracts the `review`
layer, defines `ReviewPlan`, fixes staleness to a scope hash, and gives MCP
`prepare_sccg_review` / `submit_sccg_review_result` so that built-in and
external review differ only in who performs inference (§18.3 there).

That is the plumbing, and it is already designed. **This page is about what
flows through it.** An audit against the SCCG tool-integration guideline found
that the review method is missing one of the five workflow stages entirely,
cannot express most of the repairs SCCG prescribes, and gives an external
client a materially weaker contract than the built-in path. None of that is
addressed by the sibling plan — it contains no mention of pre-checks, of which
data packages are built, or of proposing new elements.

Where the two disagree, the sibling plan and the ADRs win. This page adds; it
does not override.

---

## 2. Already decided — do not re-litigate

Recorded here so this plan is not mistaken for a competing design.

| Question | Settled by |
|---|---|
| Should built-in and MCP review share one method, contract and validator? | Yes — ADR 0013 |
| May `agent` depend on `review`? | Yes — ADR 0013; the layer gate already permits it ([check_layer_gates.cmake:68](https://github.com/lasrod/assurance-forge/blob/main/cmake/check_layer_gates.cmake)) |
| May `mcp` depend on `review` directly? | No — through `agent` only |
| How does MCP obtain a review? | `prepare_sccg_review` → external inference → `submit_sccg_review_result`, sibling plan §18 |
| When is a result stale? | Context generation or **scope** hash changed — never the global working revision, sibling plan §13.5 |
| Who may accept a suggestion? | A human. No path accepts its own — ADR 0010, ADR 0013 |
| Should concurrent AI review and MCP staging be mutually excluded? | No. The user is a third writer to the same draft (`DraftSource::Human`), so a lock cannot close the window, and MCP groups stay `Building` unboundedly. Promotion stays exclusive via `DraftWorkspaceState::Promoting`. |
| May an agent review an element it authored itself? | Yes, and the record must say so — §5.6, decided 2026-08-19 |
| Where does `EVIDENCE_BASIS` come from? | The evidence register, once it links real evidence. Exposure is then a per-item consent decision — §S4, decided 2026-08-19 |

---

## 3. Correction: Phase A1 is marked complete but is not

Sibling plan §22 marks Phase A1 **complete** (merged in
[#391](https://github.com/lasrod/assurance-forge/pull/391)). One of its tasks
did not land:

> - [ ] Move suggestion mapping.

Suggestion mapping is still `ProposalActions::CreateAiGenerated` in
`src/app/actions/proposal_actions.cpp`. Because it lives in `app`, `agent`
cannot reach it, so Phase C1 cannot meet its exit criterion "built-in and MCP
review use the same result validator" without doing this first.

`af_agent` also still links only `af_common`, and the MCP executable's link
line carries no `af_review`, so the ADR 0013 dependency was never wired even
though the gate allows it.

**Action:** reopen A1's remaining task as phase S1 below, and correct the
status line in the sibling plan from "complete" to "complete except suggestion
mapping (see SCCG conformance plan, S1)".

---

## 4. The gaps

Evidence for each is in the audit that produced this page; file references are
current as of 2026-08-19, against `main`.

### Work in flight that changes G3 and G5

Branch `feat/mcp-authoring-guidance` (phases 1–3 of
[#406](https://github.com/lasrod/assurance-forge/issues/406), design page
`docs/features/mcp-authoring-quality-plan.md`) is **not merged and has no open
PR** as of 2026-08-19, but substantially overlaps two gaps below. It:

- widens `CheckStagedArgument` from four guideline IDs to eleven — AR.1, AR.2,
  CL.2, CL.5, CL.6, EV.1, EV.7, EV.8, LF.3, RD.1, RD.4;
- adds `StagedFinding::check_id`, bound to the catalog, and serializes it to
  MCP so findings deduplicate across staging calls;
- delivers authoring doctrine through the always-land MCP channels and widens
  the prompt-quoted guideline sets;
- adds a `check_operations` pre-flight tool and refuses `submit_change_group`
  while Problem findings stand.

It does **not** touch `src/review`, `proposal_actions.cpp` or
`ai_review_controller.cpp`, so G1, G2 and G4 — and phases S1, S2, S4, S5 — are
unaffected by it.

**This plan assumes that branch lands.** S3 and S6 are scoped to what remains
after it does. If it is abandoned, S3 and S6 grow back to their pre-#406 size.

**One defect it introduces, which S3 must fix.** The AR.2 check now carries the
check ID `check-explicit-strategy`, but still tests *a strategy that develops
into nothing*. SCCG's `check-explicit-strategy` is the opposite question — "a
claim is decomposed without an explicit reasoning step", firing on a **Goal**
with children and no strategy, with `expected_data` SEL, CHILDREN, STRATEGY.
Wearing the registry's ID while answering a different question is worse than
carrying no ID: a tool that reports `check-explicit-strategy` as clean will be
read as having run SCCG's check, and it has not.

### G1 — The result contract cannot express a structural repair

`BuildExpectedAiReviewResponseSchemaText()` offers exactly two fix-carrying
fields, `suggested_element_text` and its legacy alias
`suggested_claim_wording`. Both mean *replace the reviewed element's text*.
`suggested_fix` is prose that only becomes a review-comment message.

SCCG's prescribed repair is frequently to **add** an element:

| Guideline | Repair SCCG describes |
|---|---|
| AR.2 | Insert an explicit Strategy between a goal and its children |
| EV.1 | Add a Solution, or mark the claim undeveloped |
| CL.3, AR.3, AR.6, AR.7, RD.1, RD.6 | Externalize scope, definitions or dependencies into Context / Assumption / Justification |
| SU.2, SU.9 | Convert an assumption into a Claim and re-parent it |
| SU.11 | Raise a challenge as a Counter Claim element |
| CL.5 | Define the bounding term once (`CreateTerm`) |

This is not a draft-model limitation. `core::reviews::PatchOperationType`
already defines twenty operations including every `Create*` above, and MCP
stages multi-operation groups through them today.

### G2 — Suggestion mapping stages one operation on one element

`CreateAiGenerated` resolves the anchor as the reviewed element and stages a
single `UpdateElementText`. Everything around it — revision guarding,
`DraftSource::SccgAiReview` provenance, guideline linkage, group lifecycle —
is already correct and would carry a structural group unchanged.

### G3 — Workflow stage 4, deterministic pre-checks, never runs

SCCG publishes five pre-checks in `dist/prechecks.json`. Both parsers read them
into `GuidelinesDocument::prechecks`; **nothing reads that vector.** The only
reference outside the two parsers is the field declaration.

A partial, divergent implementation exists in `core::sccg::CheckStagedArgument`,
wired only into draft staging, never into review, and not keyed by pre-check ID:

| SCCG pre-check | Guideline | Implemented |
|---|---|---|
| `check-evidence-trace` | EV.1 | Yes, as the claim-without-support advisory |
| `check-explicit-strategy` | AR.2 | **No** — the AR.2 check present is the inverse (strategy with no children) |
| `check-evidence-citation-precision` | EV.4 | No |
| `check-evidence-control-attributes` | EV.7 | No |
| `check-evidence-state-fixed` | EV.8 | No |

`check-explicit-strategy` matters most for MCP authoring: sub-goals added under
a goal with no reasoning step is the characteristic LLM output, and it is the
one that is absent.

There is a second-order defect behind it. Attaching a child allocates a **new
relationship element** (`ids.Next("R")`), and `CollectChangedFields` compares
element fields, so the parent goal is never marked `Modified` and is filtered
out of `changed_element_ids` before any check sees it. Implementing
`check-explicit-strategy` without widening that scope input would produce a
check that never fires.

### G4 — Data-package coverage

Built: `SEL`, `PARENT`, `CHILDREN`, `STRATEGY`, `DIRECT_CONTEXT`,
`INHERITED_CONTEXT`, `EVIDENCE_PATH`, `EVIDENCE_ITEM`.
Not built: `EVIDENCE_BASIS`, `PROJECT_GLOSSARY`, `STANDARD_LINKS`,
`CHANGE_HISTORY`, `USER_REVIEW_INTENT`.

Unavailability is reported honestly to the model with a `required` flag, which
is the right behaviour. Two cases still need closing:

- **`EVIDENCE_BASIS` is required by `evidence_review`.** Every evidence review
  therefore runs with a required package declared missing, degrading the
  profile SCCG designed for sufficiency judgement (EV.5, EV.6, LF.5, LF.7,
  SU.3, SU.6–SU.8) to citation hygiene. The underlying artifacts do not exist
  in the tool yet — see S4 for the decided route.
- **`PROJECT_GLOSSARY` and `CHANGE_HISTORY` describe data the tool already
  holds** — terms via `list_terms` (AF-AI-022), and review items, draft groups
  and the audit log. CL.4 and AR.6 are currently judged without definitions the
  project has already written down.

`USER_REVIEW_INTENT` has no surface at all: a user cannot say what they are
worried about.

### G5 — MCP gets a materially weaker SCCG contract

- The four prompts each quote a **hand-picked guideline ID list written in
  code** — eight IDs for `draft_argument_from_standard`, then six, five and one
  — against 48 in the catalog and 34 in `claim_review`. The guidance an agent
  gets when it **writes** a claim is a different, hand-maintained set from the
  criteria applied when the same claim is **reviewed**. It will drift as SCCG
  revises the profiles, and nothing detects that.
- No tool exposes review profiles, data packages or pre-checks.
- `StagedFinding::statement` — the guideline's own wording — is populated and
  then dropped by both serializers, so an agent receives the tool's paraphrase
  without the rule, and no `sccg://guideline/<id>` pointer to fetch it.
- Findings carry no `review_profile_id` and no statement of what was *not*
  checked, so an empty array reads as "compliant" when it means "four of
  forty-eight rules found nothing".

---

## 5. Design decisions this plan adds

### 5.1 A finding carries proposed operations

Extend the result contract with `proposed_operations[]` per finding, drawn from
the existing `PatchOperationType` vocabulary. Rejected alternative: deriving
operations from prose `suggested_fix`. The model knows which repair it means;
re-deriving it is guesswork, and a wrong guess writes into a safety argument.

`suggested_element_text` stays as the single-operation shorthand it is today,
normalized into one `UpdateElementText` so existing behaviour and tests are
preserved.

This is consistent with the sibling plan, whose validator already lists "valid
suggestion types" and "no unexpected patch operation" (§13.4) and whose
committer already creates "SCCG draft groups for supported suggestions"
(§13.6). Neither says which operations are permitted; this does.

### 5.2 The review profile's data packages define the blast radius

A review may propose operations that touch elements it was shown, plus new
elements attached to those. It may not touch a branch it never saw.

The rule is derived from SCCG rather than invented: `claim_review` is given
SEL, PARENT, CHILDREN, STRATEGY, DIRECT_CONTEXT, INHERITED_CONTEXT and
EVIDENCE_PATH, and those packages already enumerate their element IDs. Scope is
the set of IDs the plan included.

It is also the same set the scope hash covers, so one definition of "what this
review touched" serves both staleness and authorization.

Layered checks, in order, all of which already exist:

1. `core::CanAddChildElement` — legal attachment for the target's role
2. `core::sccg::CheckStagedArgument` — SCCG structural rules on the preview
3. `core::CheckGsnWellFormedness` — GSN validity on the preview

A proposal that fails is refused in the call that made it, the way MCP staging
already refuses a malformed batch.

### 5.3 Pre-checks become a real stage, keyed by SCCG ID

Add `review::sccg::RunPrechecks(model, tree, element, profile)` returning
candidate signals carrying the SCCG pre-check ID and its `interpretation`
string. Feed the results into:

- the built-in prompt, as candidate signals;
- the `prepare_sccg_review` payload, identically;
- the staging feedback path.

SCCG is explicit that these are `boolean_candidate` / `missing_fields`
signals — "candidate finding only; reviewer or AI judgment is still required".
The contract must carry that wording through rather than presenting a
pre-check hit as a finding.

### 5.4 One implementation of each mechanical check

`CheckStagedArgument` and the pre-check registry currently answer overlapping
questions differently. Consolidate on one implementation per check, in
`review`, keyed by pre-check ID, with two callers and two scopes:

| Caller | Scope | Trigger |
|---|---|---|
| Review method, stage 4 | The reviewed element and its packages | A review runs |
| Draft staging guardrail | Elements the change set touched | Every staging call |

Keep the checks that have no SCCG pre-check ID (strategy developing into
nothing, solution with children, support cycles) as structural guardrails, but
stop labelling them with a pre-check's guideline ID where they answer a
different question.

Widen the staging scope input so a parent whose child set changed is included,
per the relationship-element defect in G3.

### 5.5 Authoring guidance derives from the profile registry

Replace the hand-picked ID lists in `src/mcp/guidance.cpp` with a lookup
against the profiles an operation's output would be reviewed under, so writing
guidance and review criteria cannot drift. Add a test that fails if a prompt
cites a guideline ID absent from every profile it claims to serve.

### 5.6 Self-review is recorded, not prevented — decided 2026-08-19

An MCP-connected LLM both authors and reviews. ADR 0013's "neither path can
accept its own suggestions" governs acceptance, not review. An agent that
writes a claim, reviews it and finds nothing has produced a record that looks
like independent scrutiny and is not.

**Decided: allow it, and mark it.** An agent checking its own work before
submitting is useful and should not be blocked. What must not happen is that
the resulting record is indistinguishable from an independent review.

- Record the executor in review provenance: `internal_provider` or
  `external_client:<session_id>`. The `ReviewRunRecord` in sibling plan §14 is
  the right home.
- Flag findings — and, importantly, *clean results* — whose reviewed elements
  were authored by the same session. A self-review that reports nothing is the
  case the mark exists for.
- Surface the mark wherever a human decides on the draft: the review panel and
  the promotion surface, not only the stored record.

Rejected: refusing a self-review. It would push agents toward submitting
unchecked work, and the tool cannot tell an agent re-reviewing its own draft
from one reviewing a colleague's without the provenance this adds anyway.

---

## 6. Phases

Each depends on the sibling plan's phases as noted. Tasks are deliberately
sized so a phase is reviewable on its own.

### S1 — Finish Phase A1: move suggestion mapping into `review`

**Status: done** — `review::MapSuggestionsToDraftGroups`, with `af_review`
linked into `af_agent` and the MCP executable.

*Depends on: nothing. Blocks: S2, S5.*

- [x] Move mapping out of `ProposalActions::CreateAiGenerated` into
      `review::MapFindingsToDraftGroups`, taking the plan and validated result
      and returning group requests plus operations.
- [x] Leave `app` owning only workspace calls and status messages.
- [x] Link `af_review` into `af_agent`; add `af_review` to the MCP executable's
      link line.
- [x] Correct the Phase A1 status line in the sibling plan.

**Exit:** behaviour unchanged; existing review tests pass untouched; a caller
with no `app` and no provider can map a validated result to group requests.

**One deliberate behaviour change.** Moving the mapping put it beside the
request builder and exposed that the two disagreed about which field carries an
element's text. The request showed the model whichever of `content` /
`description` was populated, falling back to `name`; the mapper chose by element
type. So a suggestion against a Term or Expression -- whose value is `content`
-- was staged into `description`, adding text nobody reviewed and leaving the
text they objected to standing. The two now share one preference order, and an
element read by its name is repaired with `UpdateElementName` rather than an
empty-field text write.

### S2 — Scope hash, narrow form

**Status: done** — `core::reviews::ComputeScopeSemanticHash` over
`review::ReviewedElementIds`.

*Depends on: S1. Blocks: S5.*

Implements sibling plan §13.5 at the smallest useful width.

- [x] Record the reviewed element IDs on the plan (the union of the data
      packages' IDs).
- [x] Hash those elements rather than the whole model, replacing the
      `ComputeModelSemanticHash(working)` comparison.
- [x] Refuse only on scope-hash change (context generation is the MCP path's, and lands with S6).

**Exit:** an edit to an unrelated branch — by the user or by MCP — no longer
discards a completed review; an edit to a reviewed element still does. Both
directions tested.

### S3 — Pre-checks (workflow stage 4)

**Status: done** — `review::sccg::RunPrechecks`, driven by the published
`prechecks` registry, with EV.4 the last check implemented.

*Depends on: `feat/mcp-authoring-guidance` landing. Parallel with S1/S2.*

Scoped to what remains after #406 phases 1–3. Two of the five pre-checks are
substantively covered by that branch's EV.7 and EV.8 checks; this phase closes
the rest and fixes the mislabelling.

- [x] **Fix the `check-explicit-strategy` mislabelling.** Either implement the
      Goal-with-children-and-no-strategy check the ID names, or move the
      strategy-develops-into-nothing check to an ID that describes it. Do not
      ship the current pairing.
- [x] Implement `check-evidence-citation-precision` (EV.4), the remaining
      unimplemented pre-check.
- [x] Widen the staging scope input for the relationship-element defect —
      without it, a Goal-side check can never fire.
- [x] `review::sccg::RunPrechecks`, so stage 4 exists on the **review** path,
      not only at staging time. Reuse the branch's check implementations rather
      than writing a second set (5.4).
- [x] Distinguish the two registries in the code: the catalog's per-guideline
      `suggested_checks` and the published `prechecks.json` are different
      things, and a `check_id` from one must not be presented as the other.
- [x] Surface pre-check results in the review prompt, carrying SCCG's own
      `interpretation` wording.

**Exit:** all five SCCG pre-checks run, are individually tested, and are named
by their registry ID; `check-explicit-strategy` fires on a goal given children
with no strategy, including when the change that caused it only created a
relationship element; a review (not just a staging call) runs stage 4.

### S4 — Data packages

**Status: mostly done** — `PROJECT_GLOSSARY` and `CHANGE_HISTORY` supplied,
and the remaining packages name why they are absent. `USER_REVIEW_INTENT` is
plumbed but has no surface to supply it; see its task below.

*Depends on: nothing. Parallel.*

- [x] `PROJECT_GLOSSARY` from the terminology package.
- [x] `CHANGE_HISTORY` from review items. Draft groups and the audit log are not included: a review is being asked about the argument, and unaccepted proposals are not yet part of it.
- [ ] `USER_REVIEW_INTENT` — plumbed through the request contract, but **no
      surface supplies it**: the review action has no field for the user to
      state a concern, so it is reported empty in every real run. The
      remaining work is the UI field, not the package.
- [x] `STANDARD_LINKS` — deferred; no source exists. Record as a known
      limitation.
- [x] `EVIDENCE_BASIS` — **decided: stays declared-unavailable for now.** See
      below; this task only records the reason so the gap is visible rather
      than silent.

**`EVIDENCE_BASIS` and the evidence register.** The artifacts an evidence basis
would describe — coverage, thresholds, scenarios, configurations, limitations —
are not in the tool. The route is the evidence register: when it carries a link
to the actual evidence, that link becomes the source.

Two consequences that shape the work now:

- **Exposure to AI is a per-item decision, not automatic.** Once evidence is
  linked, whether a given item is available to a review is decided from the
  register, item by item. This follows the project's standing constraint that
  no data leaves without explicit user consent, and it means `EVIDENCE_BASIS`
  will be *partially* available in the normal case — some items shared, some
  withheld. The package builder must therefore distinguish three states, not
  two: available, unavailable-because-absent, and **withheld-by-choice**. A
  review told an item was withheld can say its judgement is bounded; a review
  told nothing will assume the item does not exist.
- **The register already holds part of `EVIDENCE_ITEM`.** `evidence_owner`,
  `type`, `recency`, `maturity` and `controlled_environment` in
  `ui::EvidenceRegisterRow` are close to the control attributes EV.7 and EV.8
  ask about, and are what the unimplemented pre-checks
  `check-evidence-control-attributes` and `check-evidence-state-fixed` need.
  S3 should read them rather than waiting for the link work.

**Exit:** no package is silently absent; `EVIDENCE_BASIS` carries a recorded
reason; the three-state distinction exists in the contract so adding evidence
links later is not a schema break.

### S5 — Structural proposals

**Status: done** — `proposed_operations[]` on a finding, bounded by the
profile's own data packages.

*Depends on: S1, S2. This is the phase that closes G1 and G2.*

- [x] Add `proposed_operations[]` to the result contract and schema text.
- [x] Validate against the vocabulary, the 5.2 scope rule, and the three
      layered checks.
- [x] Normalize `suggested_element_text` into one `UpdateElementText`.
- [x] Stage multi-operation groups, atomically per 13.6.
- [x] Prompt guidance describing when each `Create*` is the SCCG repair.

**Exit:** an AR.2 finding on a goal with children and no strategy produces a
draft group containing `CreateStrategy` plus the re-parenting operations, the
canvas draws it, and a human accepts it through the ordinary audited path.

### S6 — MCP parity

**Status: partly done.** The three items that do not need a review-over-MCP
tool have landed. Executor provenance and self-review flagging (5.6) wait for
phase C1 of the sibling plan, because until `prepare_sccg_review` /
`submit_sccg_review_result` exist there is no MCP review to attribute.

*Depends on: S5 and sibling plan C1.*

Scoped to what remains after #406 phases 1–3, which already add `check_id` to
the MCP payload and widen the prompt-quoted guideline sets.

- [x] Serialize `StagedFinding::statement` and an `sccg://guideline/<id>`
      pointer. `check_id` alone still gives an agent the tool's paraphrase
      without the rule.
- [x] Carry an explicit "checked / not checked" statement. (No `review_profile_id`: staging is not a review, so what the result can honestly name is the checks that ran, not a profile that did not.)
      statement on findings, so an empty array cannot be read as conformance.
- [x] Derive prompt guideline sets from the **profile registry** (5.5), with
      the drift test. #406 widens the hand-picked lists; it does not remove the
      hand-picking, so the divergence returns on the next SCCG revision.
- [ ] Executor provenance and self-review flagging (5.6).

**Exit:** a finding returned to an MCP client and one shown in the app carry
the same fields; an agent can distinguish "no findings" from "not checked".

---

## 7. Test obligations

- Pre-checks: one test per SCCG pre-check ID, named for it, including a
  negative that fails before the fix. Per the repository's verification
  practice, break the fix and watch the test fail before trusting it.
- Scope hash: unrelated edit does not invalidate; in-scope edit does; a
  `DraftSource::Human` edit is treated identically to an MCP one.
- Structural proposals: each `Create*` operation reachable from a finding;
  out-of-scope operation refused; illegal attachment refused; batch atomicity.
- Contract parity: one test asserting the built-in and MCP paths produce
  identical validated results from identical model output.
- Guidance drift: a prompt citing a guideline outside its profiles fails.
- `DraftWorkspace` tests run serially — the suite shares `.af/drafts` state.

---

## 8. Capability matrix changes

- **AF-AI-006** — currently claims "suggested corrections become independently
  reviewable SCCG draft groups". True but narrower than it reads: corrections
  are text replacements on the reviewed element only. Add that limitation now,
  before S5 changes it.
- **AF-AI-010** — the "four mechanical checks" note stays accurate through S3
  but its count changes; update with the pre-check IDs.
- New row for SCCG review through MCP once S6 lands, cited to its tests.
- Regenerate `feature-matrix.json` and run
  `python tools/features/check_feature_matrix.py` in the same change.

---

## 9. Risks

| Risk | Mitigation |
|---|---|
| Structural proposals write a malformed argument | Three layered checks on the preview before staging; refusal in the call that made it |
| A review proposes changes far from what the user selected | 5.2 scope rule, mechanically enforced from the profile's own packages |
| Pre-check false positives train users to ignore findings | SCCG's own `interpretation` wording carried through; candidates presented as candidates |
| Divergence returns between the two check implementations | S3 consolidates to one; the parity test in §7 fails if they drift |
| This plan and the sibling plan disagree | Sibling plan and ADRs win; this page adds only |

---

## 10. Tracking

- Tracking issue: [#414](https://github.com/lasrod/assurance-forge/issues/414), under parent epic
  [#388](https://github.com/lasrod/assurance-forge/issues/388), with S1–S6 as a
  task checklist.
- S1 additionally corrects the Phase A1 status line in the sibling plan.
- 5.6 is decided (2026-08-19) and lands with S6, since it depends on the
  executor provenance that phase adds.
- The per-item AI exposure decision for linked evidence (S4) is a consent
  question, not a review question. When evidence links land it should be
  designed against ADR 0005's consent rules, not bolted onto the review method.
