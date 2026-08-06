# Security Policy

## Reporting a vulnerability

**Please do not open a public issue for a security vulnerability.**

Report it privately through GitHub's
[Report a vulnerability](https://github.com/lasrod/assurance-forge/security/advisories/new)
form, which creates a draft advisory visible only to the maintainer. If that is
unavailable to you, email **jesper.brannstrom@gmail.com** with `SECURITY` in the
subject line.

Please include, as far as you can:

- What the issue is and where in the code it lives.
- How to reproduce it, ideally with a file or input that triggers it.
- What an attacker could achieve.
- The version or commit you tested.

## What to expect

Assurance Forge is maintained by one person as an open-source project. There is
no security team and no on-call rotation, so response times are best-effort
rather than contractual:

| Stage | Target |
|---|---|
| Acknowledgement of your report | within 7 days |
| Initial assessment | within 14 days |
| Fix or documented mitigation for an accepted issue | depends on severity and complexity |

You will be credited in the advisory and the release notes unless you ask not to
be. If a report is declined, you will be told why.

## Supported versions

Only the **latest release** receives security fixes. There are no long-term
support branches, and older releases are not patched.

## Scope

Assurance Forge is a desktop application that reads local files, so the risks
that matter most are about untrusted input and outbound data.

**In scope**

- Parsing untrusted SACM/XML or project files: crashes, memory-safety faults,
  XML external-entity or billion-laughs style resource exhaustion, path traversal
  when reading or writing project content.
- Silent corruption or misinterpretation of assurance data. The tool must not
  alter the meaning of a safety argument without saying so — a defect here is
  treated with the same seriousness as a memory-safety bug, because a safety case
  that silently changed is a safety problem, not just a data problem.
- Anything that sends user data to an external service without explicit consent,
  including through the AI provider and MCP integrations.
- Leakage of stored API keys or other secrets.
- Vulnerabilities in how the project builds or releases its own artifacts.

**Out of scope**

- Vulnerabilities in the bundled third-party dependencies themselves — report
  those upstream. Tell us anyway if Assurance Forge's use of one makes it
  materially worse.
- Attacks requiring an already-compromised machine or an attacker who can already
  write to the user's files.
- Findings from automated scanners with no demonstrated impact.
- Missing hardening that is not exploitable on its own.

## AI, MCP, and outbound data

Assurance Forge integrates with AI providers and can expose a safety case to an
external AI client over MCP. Both are opt-in and require explicit user consent —
see
[ADR 0005](docs/architecture/decisions/0005-provider-agnostic-ai-with-user-consent.md)
and
[ADR 0007](docs/architecture/decisions/0007-mcp-server-consent.md).

A path that sends assurance data anywhere without that consent, or that widens
what a consented action exposes, is a security issue under this policy. Report
it.

## What this policy does not claim

Assurance Forge is not certified, assessed, or approved for use in any
regulatory or safety-qualification process. This policy describes how security
reports are handled; it is not a statement of assurance about the software. See
[Status and limitations](README.md#status-and-limitations).
