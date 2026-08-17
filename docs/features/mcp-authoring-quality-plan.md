# MCP authoring quality — SCCG without being asked

- **Status:** Phase 1 implemented (`AF-AI-023` — doctrine in `initialize`
  instructions, pre-write read results and the operation schema, with the
  prompt-quoted sets widened). The structured-findings prerequisite is
  implemented: every finding carries its catalog check id and parameters, the
  panels translate by template, and a drift test holds the embedded statements
  against the catalog. Phase 2 batch 1 implemented (`AF-AI-024`,
  in-development): CL.2, CL.6, RD.1, RD.4, EV.7, EV.8 and LF.3, taking the
  mechanical set from four checks to twelve — see the correction notes under
  the candidate table. Phase 3 implemented (`AF-AI-025`): `check_operations`
  rehearses on a copy, connected and offline, and `submit_change_group`
  refuses standing problem findings unless explicitly acknowledged, with the
  acknowledgment persisted on the group and shown to the reviewer. Phase 4
  planned. Extends capability matrix row `AF-AI-010` (SCCG guidance and checks
  for an external AI client); the remaining rows land with the first
  implementing commit of their phase.
- **Date:** 2026-08-17. The problem statement below describes the coverage as
  it stood when the plan was written; phase 1 has since widened the prompt
  quotes and added the unprompted channels.
- **Architecture:** ADR 0003 — SACM XML as the source of truth (why nothing here
  rewrites an agent's argument); ADR 0005 and ADR 0013 — no second inference
  path in the server; ADR 0009 and ADR 0010 — the integrated draft and human
  promotion this plan feeds into.
- **Current state:** [MCP server → SCCG](mcp-server.md#sccg) describes what is
  built today. This page is the plan for widening it; where the two disagree
  about what exists *now*, that page and the capability matrix win.

## The problem

Users prompt their AI clients casually — *"add an argument that the braking
system is safe"* — and nothing in that prompt says what SCCG says: that a claim
is one short falsifiable proposition, that bundled properties are split into
separate goals, that the inference step is stated rather than implied. The MCP
server owns the house rules, so the burden of knowing them should sit on the
server, not on the quality of the user's prompt.

The server already carries SCCG through three mechanisms
(`src/mcp/guidance.h`): resources, prompts, and checks on staged work. But
count the coverage against the catalog's 48 guidelines:

| Mechanism | Guidelines carried | Reaches the model without the user asking? |
|---|---|---|
| Prompts | 10, quoted | **No** — the user must pick the prompt in their client |
| Resources | all 48 | **No** — the client must choose to read them |
| Staged checks | 4 (EV.1, AR.2, AR.1, CL.5) | Yes — in every staging result |

The entire SU (11), LF (7) and RD (6) families — 24 guidelines — reach an agent
only if its client reads the full catalog resource unprompted, which
mainstream clients do not do. The best content sits in the channels that
require the user to ask for it, which is exactly the user this plan assumes we
do not have.

## Design stance

Three principles, each already established elsewhere in this project and
extended here rather than invented.

**Guide before, check after, refuse rarely — never rewrite.** The tool must
never silently modify or reinterpret a safety argument (ADR 0003). Splitting a
bundled claim into two is an argument decision — *which* of the two keeps the
evidence? — so the server names the defect precisely and proposes the split,
and the agent restages. The reasoning stays attributable to the author, and the
human remains the only party who accepts anything.

**Derive from the catalog, never invent.** The existing checks embed SCCG's own
wording so a check cannot drift from the guideline it serves, and the prompts
quote the catalog for the same reason. The catalog meets this halfway: every
guideline publishes a `tool.suggested_checks` id (`check-single-property`,
`check-claim-length-and-role-mixing`, …) and the dist file ships five
machine-oriented `prechecks` with expected data and the interpretation
*"Candidate finding only; reviewer or AI judgment is still required."* Two of
those five are implemented today (`check-evidence-trace` → EV.1,
`check-explicit-strategy` → AR.2). Every new check binds to a catalog check id,
and the catalog's own `bad`/`good` examples become its test corpus: a check
ships only if it fires on the guideline's `bad` example and stays silent on its
`good` one.

**Honest scope.** Most of SCCG is prose only a reader can judge. A green
mechanical result must never read as "SCCG compliance", findings never block
promotion, and each finding names the guideline so a reviewer can check the
rule rather than take the tool's word for it. All of this holds today
(`src/core/sccg/staged_checks.h` states it as doctrine) and continues to hold
at twenty checks exactly as it does at four.

## The channels, ranked by how reliably they land

The plan's ordering principle: invest first in the channels that reach the
model without anyone asking, because those are the only ones that help the
casual prompt.

| Channel | Lands unprompted? | Carries today | Under this plan |
|---|---|---|---|
| Refusal at submit | Always, deterministically | nothing | Problem-severity shapes (phase 3) |
| Findings in staging results | Always — models act on their own tool output | 4 guidelines | ~20 checks (phase 2) |
| Guidance on read results | Reliably, just before writing | nothing | authoring doctrine (phase 1) |
| `instructions` at `initialize` | Automatic, client-dependent | connection status | doctrine summary (phase 1) |
| Tool and schema descriptions | Always in context at generation time | direction, translation, terminology rules | claim-writing rules on the `text` field (phase 1) |
| Prompts and resources | Only when the user asks | 10 quoted / full catalog | widened quotes; unchanged role |
| Client sampling | Server-initiated; client support uneven | not used | judgement-level critique (phase 4) |

## Phase 1 — carry the doctrine in the channels that always land

No new subsystem; three placements of one compact text.

**A house authoring doctrine, stated once.** A ~15-line condensation of the
rules an agent most needs while its hands are on the keyboard: one claim per
goal, short and falsifiable; no bundled properties; no inference words inside a
claim — decomposition is structure, not sentence syntax; goals assert,
strategies reason, solutions name the fact an artifact establishes; bound every
qualifier or define the term; mark what is unsupported undeveloped rather than
inventing evidence; cite evidence precisely, at a fixed version. Each line
names its guideline id, and a test asserts every named id exists in the loaded
catalog, so the condensation cannot outlive the catalog it condenses.

**Placement 1 — `initialize.instructions`.** Today this field carries only the
connection mode (`src/mcp/server.cpp`). It keeps that and gains the doctrine.
Clients differ in whether they surface `instructions` to the model, which is
why this placement is not sufficient alone.

**Placement 2 — read results.** The reads that precede writing —
`get_case_overview`, `get_argument_tree`, `suggest_placement`,
`get_draft_status` — gain a constant `authoring_guidance` block: the doctrine
plus a pointer to `sccg://guidelines`. This is the just-in-time channel: an
agent that was told nothing still reads before it writes, and the rules arrive
with the reading. `get_element` and `find_elements` stay lean — they are called
in loops, and guidance repeated fifty times per conversation is noise that
teaches the model to skip it.

**Placement 3 — the operation schema.** The single highest-leverage sentence in
this plan is the description of the `text` property in `OperationsSchema`
(`src/mcp/tools.cpp`), because it is in the model's context at the exact moment
it generates the words that become a claim. It gains the one-claim rule and the
role rule. `begin_change_group`'s description gains one sentence pointing at
the doctrine.

The prompts also widen their quoted sets (the SU, LF and RD families are
currently quoted nowhere), but prompts remain the opt-in channel and are not
what this phase is for.

## Phase 2 — widen the mechanical checks

`core::sccg::CheckStagedArgument` grows from 4 checks to roughly 20, staying
inside its stated doctrine: structural or lexical signatures only, each bound
to a catalog check id, each individually tested, everything else left to a
reader. Because the checks run in `core` against the materialized preview, the
same widening reaches all three consumers at once: MCP staging results, the
review panel, and the Draft Changes panel — the human authoring flow gets every
check the agent gets.

**The acceptance rule for a new check.** A check ships only with all of:

1. A catalog binding — the guideline's `suggested_checks` id, so the check set
   is SCCG's, not ours.
2. A test per rule (the existing `test_sccg_staged_checks.cpp` convention),
   including firing on the guideline's `bad` example and staying silent on its
   `good` example.
3. Severity **Advisory** unless a reviewer would certainly reject the shape.
   Lexical checks are the ones most likely to be wrong; a check that cries wolf
   gets ignored, which is worse than not having it.
4. A drift test asserting the embedded guideline wording still matches the
   catalog — the constants stay in the C++ for self-description, and the test
   is what keeps twenty embedded statements honest where four could be kept
   honest by eye.

**The candidate set.** Signatures marked *firm* are expected to survive tuning
unchanged; *tune* means thresholds or word lists need calibrating against real
cases before shipping.

| Guideline | Catalog check id | Decidable signature | Severity | Confidence |
|---|---|---|---|---|
| CL.1 | `check-claim-is-proposition` | Claim text with no predicate (bare noun phrase), or requirement phrasing ("shall", "must ensure") in place of a proposition | Advisory | tune |
| CL.2 | `check-single-property` | Coordinating conjunction joining distinct properties in one goal ("safe and secure", "complete and correct", "as well as") | Advisory | firm |
| CL.3 | `check-claim-length-and-role-mixing` | Sentence and word count over threshold; enumeration markers the catalog names ("including", "covering", "considering", "with respect to") introducing a topic list | Advisory | tune |
| CL.4 | `check-claim-ambiguity` | The lexical fragment only: "appropriate", "adequate", "as necessary", "etc." — the same pattern as the existing CL.5 list | Advisory | firm |
| CL.6 | `check-claim-step-mixing` | Inference connectives inside one claim ("because", "therefore", "since", "which means") | Advisory | firm |
| RD.1 | `check-element-signposting` | Role/text mismatch: a goal or solution whose text opens with strategy signposting ("Argument over…", "Argument by…") | Problem | firm |
| RD.4 | `check-promotional-language` | Promotional adjectives ("excellent", "world-class", "state-of-the-art", "best-in-class") | Advisory | firm |
| EV.3 | `check-claim-subject-not-document` | Solution text that is only an artifact name — a filename or a bare "X report" with no stated fact | Advisory | tune |
| EV.4 | `check-evidence-citation-precision` | Evidence reference with no section, clause, table or test id — a dist precheck today unimplemented | Advisory | tune |
| EV.7 | `check-evidence-control-attributes` | Evidence reference carrying none of owner, version, date, status — a dist precheck, `missing_fields` | Advisory | firm |
| EV.8 | `check-evidence-state-fixed` | Evidence reference to mutable state: a URL or path containing "latest", "current", "main", "trunk" — a dist precheck | Advisory | firm |
| LF.1 | `check-circular-support` | Textual near-duplication between parent and child claim, complementing the existing graph-cycle check | Advisory | tune |
| LF.3 | `check-completeness-vs-absence` | Support that argues from absence: "no evidence of", "no failures observed", "no issues found" | Advisory | firm |
| LF.6 | `check-stated-precision` | High-precision figures with no accompanying uncertainty or confidence wording | Advisory | tune |
| AR.8 | `check-claim-text-purity` | Inference connectives in a description or rationale field of a claim whose visible support does not carry that argument | Advisory | tune |
| SU.2 | `check-explicit-assumptions` | An assumption element whose statement carries no justification and none is attached | Advisory | tune |

Deliberately **not** in this set, with the reason recorded so nobody re-litigates
it per check: RD.5 (passive voice hiding agency — lexically too noisy),
AR.5 (scope and terminology drift across a decomposition — needs reading both
levels), AR.4, EV.6, LF.2, LF.4, LF.7, SU.1, SU.4 and the rest of SU — all
judgement. They are phase 4's material.

**Corrections the catalog forced during batch 1** (CL.2, CL.6, RD.1, RD.4,
EV.7, EV.8, LF.3 — implemented; the rest of the table still pending):

- **The connectives signature belongs to RD.1, not CL.6.** The catalog's CL.6
  is about chaining lifecycle steps — its bad example is "mitigated and
  validated" — so the implemented CL.6 check pairs step verbs. "Because" in a
  claim is the catalog's own RD.1 bad example, so the connectives check
  (`because`, `therefore`) reports under `check-element-signposting`.
- **The "Argument over…" goal-opener signature is deferred.** It was this
  plan's invention; no catalog hint backs it, and RD.1's template is already
  taken by the connectives case. It returns, if at all, with its own evidence.
- **CL.4 is deferred, not firm.** Its "lexical fragment" list ("appropriate",
  "adequate") appears nowhere in the catalog — implementing it would break the
  rule that word lists derive from the guideline. It waits for either a
  catalog word list or the phase 4 judgement channel.
- **EV.7/EV.8 marker sets** are derived from the statements, hints and the
  examples' own wording (`rev`, `approved`, `snapshot`, `captured`,
  `confluence`, `wiki`), and both checks accept a four-digit year as a fixing
  marker.

## Phase 3 — rehearsal and refusal

**A pre-flight check tool.** Today the only way for an agent to get findings
is to stage — which draws on the user's canvas, then flickers away if the
agent revises. A `check_operations` tool runs the same validation and checks
as `stage_operations` against the same materialized preview and stores
nothing: no draft mutation, no revision change, nothing on the canvas. An
agent can iterate privately until its work is clean and then stage once. The
tool works offline too — rehearsal against the accepted copy is read-only, so
the offline refusal of writes does not apply to it.

**Refusal at submit, not at stage.** Staging is deliberately incremental — a
strategy staged in one call gets its children in the next — so every
intermediate shape is legitimately unfinished, and refusing Problem findings at
`stage_operations` would break the workflow the tools exist to support. The
gate belongs where the agent declares itself finished: `submit_change_group`
refuses while Problem-severity findings stand against the group, naming each
one. The escape hatch is explicit: `acknowledge_findings: true` submits anyway
and records the acknowledgment on the group, so a reviewer sees that the shape
was flagged and the agent (or its user) chose to proceed. An agent can always
finish; it can no longer silently hand a reviewer a strategy that develops
into nothing.

Advisory findings never refuse anything, at either point.

## Phase 4 — judgement where mechanics end

Whether a decomposition is complete, evidence relevant, an assumption
reasonable — the majority of SCCG — can only be judged by a reader. Two routes
exist, and the layer rule (`mcp/` must not include `ai/`, ADR 0013) shapes
both:

**Client sampling, preferred.** MCP `sampling/createMessage` lets the server
ask the *client's* model to review staged text against quoted guidelines and
return findings labeled as judged, not mechanical. Inference stays in the
client the user already chose and pays for; no API key enters the server, no
new egress path opens, and the case content sent is content that client
already holds under its existing grant. Client support is uneven, so the
capability is feature-detected at `initialize` and degrades to absence.

**The in-app review, already built.** The SCCG AI review (AF-AI-006) already
reviews the working draft including MCP groups, and its findings become draft
groups the agent can see through `get_draft_events`. Phase 4's fallback is not
new machinery — it is workflow text telling the agent to ask the user to run
the review when sampling is unavailable.

## Findings become structured

Today a finding's `detail` is a hand-written English sentence, and two UI
surfaces show it raw — so every check added in phase 2 would grow untranslated
text in the review panel and Draft Changes panel, which the i18n policy does
not allow to accumulate. Before phase 2 lands, `StagedFinding` gains the
catalog `check_id` and the parameters the sentence interpolates (the offending
term, the element role). `core` keeps storing English — the layer rule forbids
it `ui::i18n` — and the panels translate by template msgid at display, the
established pattern for data-borne English. The MCP surface keeps serializing
the English sentence plus the new `check_id`, which also gives agents a stable
key to deduplicate and act on.

## Non-goals

- **No automatic rewriting.** The server never splits, shortens or rewords a
  staged claim, however confident the diagnosis. It names the defect and the
  agent restages (ADR 0003).
- **No "SCCG compliance" claim.** The mechanical set is a named subset plus
  advisory prose, and every surface that reports findings says so.
- **No blocking of promotion.** Refusal exists only at agent submit time; the
  human reviewer's authority over acceptance is untouched.
- **No score.** A numeric grade would be read as conformance by exactly the
  people the capability matrix warns about.
- **No inference in the server.** Judgement-level critique borrows the client's
  model or the application's existing review; the MCP executable never calls a
  provider (ADR 0005, ADR 0013).

## Capability matrix rows

Added with the first implementing commit of each phase, `planned` until then —
per the matrix rules, citing no tests while planned:

| Row | Capability | Phase |
|---|---|---|
| AF-AI-023 | Authoring doctrine delivered in every session (instructions, read results, operation schema) | 1 |
| AF-AI-024 | Widened mechanical SCCG checks, catalog-bound and example-tested | 2 |
| AF-AI-025 | Pre-flight `check_operations` and submit-time refusal of Problem findings | 3 |
| AF-AI-026 | Judgement-level critique via client sampling, degrading to the in-app review | 4 — `candidate` until client support is surveyed |

`AF-AI-010`'s Notes update as each phase changes what "SCCG guidance and
checks" means.

## Verification

What must be true before each phase is finished, in the repository's terms:

**Phase 1.** The stdio smoke test asserts `initialize.instructions` carries the
doctrine and that every guideline id it names resolves in the loaded catalog.
`test_mcp_modes.cpp` asserts the four pre-write reads carry
`authoring_guidance` and that `get_element` and `find_elements` do not.

**Phase 2.** One test per rule in `test_sccg_staged_checks.cpp`, each observed
to fail when its check is deliberately broken — a green negative test proves
nothing until it has been seen red. Every check fires on its guideline's `bad` example and is silent on the `good`
one, read from the catalog rather than copied into the test. A drift test
compares every embedded statement against the catalog. The i18n catalog check
stays green — which forces the structured-findings work to precede the check
widening.

**Phase 3.** `check_operations` leaves the draft revision unchanged and
`.af/drafts` byte-identical, connected and offline. `submit_change_group`
refuses with Problem findings, names them, and records an acknowledgment when
overridden; the registry-wide no-promote test still passes.

**Phase 4.** Sampling is invoked only when the client advertised the
capability; its absence produces no error and no degraded finding quality
claim. Judged findings are labeled as judged everywhere they appear.
