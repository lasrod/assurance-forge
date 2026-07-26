# GSN well-formedness and argument quality

Two different things live here and should not be conflated:

- **Structural rules** — violated, the diagram is not valid GSN. A tool can
  decide these.
- **Argument-quality problems** — the diagram is valid GSN but the argument is
  weak or misleading. A tool can *flag* these; only a human can settle them.

Assurance Forge reports both through `core::problems` (see
`src/core/problems/problems_manager.cpp`). Keep the distinction visible in the
message: a structural violation is a defect, a quality flag is a prompt.

## Structural rules

1. **Solutions are leaves.** Nothing is `SupportedBy` a Solution.
2. **Context, Assumption and Justification are leaves of `InContextOf`.** They do
   not support anything and are not supported.
3. **`SupportedBy` sources are Goals or Strategies only.** (v2.2 OCL constrains
   this; it is also how the notation is read.)
4. **`InContextOf` targets are Context, Assumption or Justification only.**
5. **A Strategy must be supported.** A Strategy with no `SupportedBy` children is
   a reasoning step that reasons to nothing.
6. **No cycles in `SupportedBy`.** A goal that transitively supports itself
   proves nothing. This is the single most damaging structural defect because it
   reads as a complete argument.
7. **Every element has an identifier**, and identifiers are unique within their
   module (v3 makes this mandatory).
8. **Away elements name their source module**, and that module must exist.
9. **A Challenge targets exactly one element or relationship**, and the target
   must exist.
10. **An undeveloped element has no `SupportedBy` children.** Decorated *and*
    supported is contradictory — either the decorator is stale or the support is.

## Argument-quality problems

Ordered roughly by how often they appear in real safety cases.

**Goals that are not propositions.** "Hazard analysis", "Software testing",
"Safety requirements" are topics. A goal must be capable of being true or false:
"All hazards identified in the PHA have been mitigated to ALARP". A topic-goal
cannot be supported, only elaborated, so the argument below it is decorative.

**Unsupported leaf goals with no undeveloped decorator.** The argument silently
claims completeness it does not have. Contrast with a properly decorated
undeveloped goal, which is honest.

**Strategy that restates its parent.** "Argument that the system is safe"
supporting "The system is safe" adds no reasoning step. A Strategy earns its
place by naming the *decomposition principle* — over hazards, over subsystems,
over lifecycle phases — and the reader should be able to ask "is that
decomposition complete?"

**Incomplete decomposition.** A Strategy decomposing over a set (hazards,
requirements, components) needs a Context that fixes the set, otherwise "argue
over each hazard" is unfalsifiable. Missing that Context is the most common way
a well-drawn argument turns out to prove nothing.

**Claims smuggled into Context.** Context is for scope and definitions. A Context
node asserting a contingent fact is an unsupported claim hiding where nobody
looks for one — it should be a Goal (and supported) or an Assumption (and
acknowledged).

**Assumption used where support is required.** Assuming what the argument is
supposed to establish. Look for Assumptions near the top of the tree.

**Solution that is a claim, not a reference.** "Testing shows the system is
safe" is a goal. A Solution names the artifact: "Test report TR-42 §5".

**Evidence that does not discharge its goal.** The Solution is a real artifact
but does not establish the specific proposition above it — a unit-test report
under a system-level claim. Only a human can judge this; the tool can surface
the pairing.

**Single point of failure in evidence.** Every branch resting on one artifact or
one technique.

**Over-long node text.** A goal that needs a paragraph is usually several goals,
or one goal plus Context.

**Missing counter-consideration.** A mature argument records what would defeat
it. Total absence of dialectics in a substantial case is worth noting, though
never an error.

## Reviewing an argument

Read bottom-up, not top-down. Top-down reading follows the author's intent and
is easy to agree with; bottom-up asks of each step "does *this* actually give me
*that*?" and is where decomposition gaps surface.

For each `SupportedBy` fan-out, ask the two questions in order:

1. **If every child were true, would the parent be true?** (Sufficiency.) This is
   where the missing set-defining Context shows up.
2. **Is each child actually established?** (Soundness.)

The Safety Case Core Guidelines catalog (`src/parser/sccg_dist_parser.cpp`,
loaded from the `external/safety-case-core-guidelines` submodule) is the
project's structured form of this review guidance and is what the AI review
path uses. Prefer adding a guideline there over hardcoding a new check.
