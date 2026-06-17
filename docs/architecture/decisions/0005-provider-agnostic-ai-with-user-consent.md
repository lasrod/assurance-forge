# 0005. Provider-agnostic AI with explicit user consent

- Status: Accepted
- Date: 2026-06-17
- Deciders: Assurance Forge maintainers

## Context

Assurance Forge offers AI-assisted review of assurance arguments. This raises
two concerns. First, assurance-case content is sensitive user data, and sending
it to a third-party AI provider is a meaningful action the user must control.
Second, the AI landscape changes quickly; binding the tool to a single vendor's
SDK would create lock-in and limit users who must use a specific provider for
policy or cost reasons.

The project's engineering principles state that user data belongs to the user at
all times, that no data may be sent externally without explicit consent, and
that AI integrations must be transparent and user-controlled.

## Decision

We will implement AI features as a provider-agnostic capability owned by the
`ai` layer (AI settings, prompt construction, provider calls, response parsing,
and background task execution), with configurable provider profiles rather than a
hard dependency on any single vendor.

No assurance-case data is sent to an external provider without explicit user
consent. The prompts and the data being sent are transparent to the user, and AI
output is surfaced as suggestions (reviews, proposed patches, problems) that the
user reviews — it never silently mutates the safety argument.

## Consequences

- Users choose their provider and remain in control of when and what data leaves
  their machine.
- The tool avoids lock-in to a single AI vendor and can adopt new providers by
  adding profiles rather than rewriting integrations.
- Changes to AI response handling require tests, consistent with the project's
  testing rules for model-affecting code.
- Supporting multiple providers and a consent/transparency surface is more work
  than wiring a single SDK directly, and provider differences must be normalized
  within the `ai` layer.
