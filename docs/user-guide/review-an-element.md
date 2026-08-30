# Review an element

Assurance Forge does not judge whether a safety case is adequate. It surfaces
findings, records who accepted what, and keeps the history — the judgement stays
with the reviewer.

## Problems

The **Problems** tab lists findings with a severity, the source that raised
them, the element, a message, the SCCG guideline where one applies, and a **Fix**
action where one exists. The filter chips (`All`, `Validation`, `Review`,
`Warnings`, `Info`) carry live counts, and the status bar shows the same totals.

Findings come from model validation, GSN v3 well-formedness checks, terminology
checks, register checks and review outcomes. Clicking a row selects the element
the finding is about.

## Manual review

Right-click an element → **Mark review OK manually**. The reviewer name from
**Edit → Preferences** is recorded with it, so a review says who accepted it; if
no name is set yet, the application asks for one before recording.

## SCCG-guided AI review

Right-click an element → **AI Review**. The profile is selected from the
[Safety Case Core Guidelines](https://github.com/lasrod/safety-case-core-guidelines)
catalog for the element's kind, and the result comes back as review findings
against named guideline IDs.

Three things are worth knowing before using it:

- **Nothing leaves the machine without an explicit action of yours.** AI is off
  until configured, and the provider, model and key are yours
  ([ADR 0005](../architecture/decisions/0005-provider-agnostic-ai-with-user-consent.md)).
- **It reviews, it does not validate.** Findings are advisory. `AI review OK` is
  set by an AI outcome and is not a substitute for a human review.
- Mechanical SCCG checks — the deterministic pre-checks — run without any AI at
  all, and report by catalog check id.

Configure providers in **Edit → Preferences → AI**. See
[Review and AI](../architecture/review-ai.md) for how the review layer stays
independent of the provider.

## Review comments and proposals

The **Review** tab holds review comments, the SCCG guideline browser, and
change proposals — a proposed edit kept beside the argument until someone
accepts it. Proposals are previewed on the canvas before they are applied.

## History

Every applied command is one audited transaction. The **History** tab and the
timeline under the canvas (`S0` … `NOW`) reconstruct the argument as it stood at
any recorded point; pinning a past transaction makes the canvas and inspector
read-only until you return to `NOW`.

**Baselines** and **snapshots** mark points worth returning to. Accepting a
working draft is recorded as its own transaction and becomes the trusted replay
root, so the history after an accept is continuous rather than a gap.

If the replayed history stops reproducing the SACM on disk — normally because a
file was edited outside the application — the canvas says so and offers
**Reconcile audit log**, which archives the current `.af/` artifacts and rebuilds
from the file. It is a repair, not a silent correction: it tells you the history
before that point is no longer replayable.
