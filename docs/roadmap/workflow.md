# Roadmap Workflow

Assurance Forge uses GitHub Discussions for early ideas, GitHub Issues for roadmap epics and implementation tasks, GitHub Projects for planning fields, and MkDocs for public documentation.

For the public-facing status and direction view, see [Product Roadmap](public.md).

Anyone may submit a roadmap request. A maintainer must approve the request before automation creates an epic, subtasks, sub-issue links, or Project entries.

## Contributor Flow

1. Open a new issue.
2. Select **Roadmap Epic Request**.
3. Fill in the roadmap form.
4. Submit the issue.
5. Wait for maintainer review.

Opening the request only triggers validation. It does not create subtasks or add anything to the roadmap Project.

## Maintainer Flow

1. Review the roadmap request.
2. Edit the request if scope, tasks, or metadata need adjustment.
3. Confirm the `AF-E-xxxx` epic ID.
4. Apply the `roadmap-approved` label.
5. Review the generated epic body, generated task issues, sub-issue links, and Project fields.

Only actors with `admin`, `maintain`, or `write` repository permission may trigger generation. If an untrusted actor applies `roadmap-approved`, the automation stops, comments on the issue, removes the approval label where possible, and applies `roadmap-failed`.

## Labels

The request form applies these labels when they already exist:

- `roadmap-request`
- `needs-roadmap-review`

The automation also ensures the roadmap labels exist before it uses them. The issue form never applies `roadmap-approved`; that label is reserved for maintainer approval.

Maintainers may apply `roadmap-retry` to an existing generated epic when follow-up Project or sub-issue work should be retried deliberately.

Generated epics receive:

- `type: epic`
- `roadmap-generated`
- the matching `area:*` label

Generated tasks receive:

- `type: task`
- `roadmap-generated`
- the matching `area:*` label

## Maturity And Points

Roadmap requests use these maturity stages:

| Maturity | Meaning |
|---|---|
| Candidate | Worth considering; needs scope, risks, and estimate. |
| Planned | Accepted into the roadmap. |
| Prototype 1 | Minimal working version that proves the concept. |
| Prototype 2 | Integrated version using real architecture, persistence, and UI patterns. |
| Ready | Documented, tested, stable, and suitable for normal use. |
| Deferred | Valid idea, but not planned now. |

Size Points estimate total effort. Completed Points are calculated from maturity:

| Maturity | Completion |
|---|---:|
| Candidate / Planned / Deferred | 0% |
| Prototype 1 | 30% |
| Prototype 2 | 70% |
| Ready | 100% |

## Project Setup

Create a GitHub Project named **Assurance Forge Roadmap** and store its number in the repository variable `ROADMAP_PROJECT_NUMBER`. Use the number from the Project URL, for example `https://github.com/users/OWNER/projects/NUMBER` or `https://github.com/orgs/OWNER/projects/NUMBER`.

By default, the automation looks for the Project under the repository owner. If the Project belongs to a different user or organization, store that login in the repository variable `ROADMAP_PROJECT_OWNER`.

Required Project fields when `ROADMAP_PROJECT_NUMBER` is configured:

| Field | Type |
|---|---|
| AF ID | Text |
| Area | Single select |
| Maturity | Single select |
| Size Points | Number |
| Completed Points | Number |
| Priority | Single select |
| Target Release | Text or single select |
| Public Roadmap | Single select |
| Discussion URL | Text |
| Automation Status | Single select |

Project field names and single-select options must match exactly. If a field is missing or a single-select option does not exist, the automation comments on the issue, applies `roadmap-failed`, and prints the missing Project setup details in the Actions log.

Use the built-in `GITHUB_TOKEN` for issue operations. User-level and organization-level GitHub Projects may not be visible to the built-in token, even when `ROADMAP_PROJECT_NUMBER` is correct. If Project lookup or updates fail for a valid Project URL, add a `ROADMAP_TOKEN` secret with access to the repository issues and the target Project. That token should have the narrowest repository and Project permissions possible.

## Failure Recovery

The generation workflow is idempotent. It checks for the generated epic marker and existing generated task titles before creating issues.

If sub-issue linking or Project updates fail, the automation keeps created issues, adds `roadmap-failed`, and comments with the failure. Fix the configuration or permissions, then rerun by removing and reapplying `roadmap-approved`, or by applying `roadmap-retry` to the generated epic.

If an already-generated epic is approved again while it is not marked `roadmap-failed` and does not have `roadmap-retry`, the automation leaves the epic and tasks unchanged and does not retry Project updates. This prevents a successful epic from being downgraded to `roadmap-failed` because of a transient follow-up API error during a harmless reapproval.