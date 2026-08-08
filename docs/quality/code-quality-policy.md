# Code quality policy

What is enforced mechanically, what is only measured, and what is not checked at
all. Companion to the [repository quality baseline](repository-baseline.md),
which records the numbers at a point in time; this page records the rules —
including, under [Refactoring discipline](#refactoring-discipline), the ones a
reviewer enforces rather than a machine. Where cleanup should start is the
[hotspot register](hotspot-register.md).

## Formatting

C++ formatting is enforced at commit time, not in CI. `cmake --preset default`
installs `.githooks/pre-commit` via `core.hooksPath`, and the hook runs
clang-format over what is staged. A file staged with unstaged edits alongside
it is reported rather than rewritten, so formatting never sweeps
work-in-progress into a commit.

**There is no CI format gate, and the absence is deliberate**, recorded here so
it is not read as an oversight. The hook runs on every machine that configured
the project the documented way; the failure a gate would catch is a contributor
who bypassed hooks, which has not happened. If it does, this decision gets
revisited with that evidence in hand.

Configuration is `.clang-format` at the repository root: `ColumnLimit: 120`,
and `SortIncludes: Never` because include order is semantic on Windows and for
some third-party headers — a formatter must not reorder it.

Only C++ has a formatter. CMake, Python, Markdown, YAML and JSON do not — see
[Not yet enforced](#not-yet-enforced).

## Compiler warnings

Until this policy existed, the build set `/FS /utf-8 /MP` on MSVC and nothing
warning-related on any compiler. There was no `-Wall`, no `-Wextra`, no `/W4`,
no `-Werror`. **The project compiled at each compiler's default warning level**,
so any claim about warning cleanliness was unfounded in either direction — the
warnings had not been suppressed, they had never been requested.

| Compiler | Level |
|---|---|
| MSVC | `/W4 /w14505 /w15245` |
| GCC, Clang | `-Wall -Wextra` |

Applied **per target**, never globally. A global `add_compile_options()` would
also hit hello_imgui, curl, yaml-cpp and the rest of the fetched dependencies,
and drowning a dozen of our own warnings in thousands of theirs is how a warning
level gets switched back off a week later.

Subsystems are listed explicitly in `src/CMakeLists.txt` rather than globbed. A
new subsystem should have to state that it builds warning-clean, not inherit the
claim by being in the right folder.

Coverage is not limited to the per-layer object libraries. `assurance-forge`,
`assurance-forge-mcp`, `tests`, `sacm_cli` and `sacm_tests` carry translation
units of their own -- `src/app/main.cpp`, `src/mcp/main.cpp`, and every test
source -- and warning only on the libraries would leave the ratchet with a hole
in the files a newcomer opens first.

`/w14505`, `/w15245` and `/w15264` are off by default even at `/W4`. Each is
MSVC's equivalent of a GCC/Clang diagnostic that already fails this build:

| MSVC | Equivalent | Catches |
|---|---|---|
| `/w14505`, `/w15245` | `-Wunused-function` | A `static` or anonymous-namespace function nothing calls |
| `/w15264` | `-Wunused-const-variable` | A constant nothing reads |

Without them a Windows developer builds clean and CI rejects the branch on GCC
and Clang, which teaches people that the local build is not worth running.

Turning them on found **twelve** pieces of dead code MSVC had been silent about
— ten functions and two constants. Six of the functions were in
`terminology_package_service.cpp`, duplicating helpers that also live in
`terminology_internal.cpp`; the two constants were in `project_service.cpp`,
duplicating ones that `project_manifest.cpp` actually reads.

### Checking other compilers' diagnostics without waiting for CI

MSVC has no equivalent for some `-Wextra` diagnostics. Before pushing, sweep the
first-party sources with **clang**:

```bash
# run the same command with g++ as well
clang++ -std=c++23 -fsyntax-only -Wall -Wextra     -Wno-missing-field-initializers -D_CRT_SECURE_NO_WARNINGS     -Isrc -Ilibs/sacm/include -Ilibs/sacm/src     -Iexternal/hello_imgui/src -Iexternal/hello_imgui/external/imgui     -Iexternal/hello_imgui/external/imgui/backends     -Iexternal/hello_imgui/external/imgui/misc/cpp     -Iexternal/pugixml/src -Iexternal/picosha2     -Iexternal/nativefiledialog-extended/src/include     $(for d in build/_deps/*-src/include build/_deps/*-src/single_include; do echo -I"$d"; done)     <file>.cpp
```

**Run both clang and GCC. Neither alone is sufficient**, and this was
established by testing rather than assumed:

| Diagnostic | MinGW GCC | Clang |
|---|---|---|
| `panel_hover` unused variable in `theme.cpp` | silent | **caught** |
| `-Wdangling-else` on an unbraced `if` guarding a gtest macro | **caught** | silent |

The GCC miss is not about `-fsyntax-only` — it stayed silent under `-c -O0` and
`-c -O1` too, so it is the compiler build. The clang miss is specific to gtest:
clang reports the same pattern in a hand-written macro, but gtest's expansion
suppresses it while GCC still warns. A synthetic test of that pattern therefore
says both compilers catch it, and is wrong about the code that matters.

Each of these reached CI because a sweep with one compiler reported clean.

This is still a proxy, not a build. CI remains the authority.

### Warnings are errors in CI, not locally

`AF_WARNINGS_AS_ERRORS` defaults to `OFF`. CI passes `-DAF_WARNINGS_AS_ERRORS=ON`
on all three platforms.

The asymmetry is deliberate. A newer compiler than CI's, emitting a diagnostic
nobody has seen yet, should not stop a contributor building the project — but it
should stop the merge. The person who can act on a new warning is the one
opening the pull request, not the one who happened to install a newer GCC.

## Suppressions

Each is scoped to one target, and each needs a reason. An undocumented
suppression is indistinguishable from a fixed problem.

| Suppression | Scope | Why |
|---|---|---|
| `_CRT_SECURE_NO_WARNINGS` | First-party targets, MSVC only | `std::getenv` and `std::fopen` are standard C++ that MSVC deprecates on its own authority. Its replacements (`_dupenv_s`, `fopen_s`) are not portable, and this code builds on three toolchains, so the warning has no action behind it. It does **not** cover STL deprecations. |
| `-Wno-missing-field-initializers` | First-party targets, GCC and Clang | Fires on partial aggregate initialization, which this codebase uses deliberately: a callbacks struct is built positionally for the members that have one, and the rest are assigned by name immediately below. Listing every member in the braces would duplicate those assignments. Both compilers already exempt `{}` from it. |
| `/wd4456` | `sacm` target, MSVC only | Sixteen instances of one pattern in the XMI reader and writer: `else if (auto* pkg = dynamic_cast<...>)` chains where MSVC counts the previous branch's variable as still in scope. None is a live shadowing bug. Renaming means editing `libs/sacm/src`, which carries a conformance obligation that does not belong in a change about warning levels. |

None of the three hides a defect. Where a warning did point at one, it was
fixed:

- `std::filesystem::u8path` is deprecated in C++20 and was **replaced**, not
  silenced — see [UTF-8 paths](#utf-8-paths) below.
- Unreachable code, two unused locals, an unreferenced parameter and a
  `class`/`struct` mismatch were all removed.

## UTF-8 paths

`core::PathFromUtf8` replaces `std::filesystem::u8path`. It is not
interchangeable with `std::filesystem::path(value)`:

> That constructor reads a narrow string in the **native** encoding — the active
> code page on Windows — so passing UTF-8 to it mangles every non-ASCII path,
> silently, and only for the users who have them.

The test uses Japanese and accented paths deliberately. An ASCII-only test
passes against both the correct and the broken conversion, so it would prove
nothing.

## Static analysis

clang-tidy runs over the 255 production translation units and is ratcheted
against a committed baseline: findings may fall, never rise. Full rationale,
current findings and known gaps are in [static analysis](static-analysis.md).

| Aspect | Position |
|---|---|
| Configuration | `.clang-tidy` at the repository root, so an IDE reports what CI reports |
| Enabled set | 69 findings (90 when first measured; [#306](https://github.com/lasrod/assurance-forge/issues/306) closed the 21 correctness ones). The four families with no exclusions give 4,784; nine excluded checks account for 4,694 of them (98%), each recorded with its count and reason |
| Enforcement | `run_clang_tidy.py --check` as its own CI job: changed `.cpp` files on a pull request, the full sweep on `main` |
| Incomplete runs | A translation unit clang-tidy cannot compile aborts the run. It contributes no findings, so continuing would record unanalyzed code as clean |
| Auto-fixing | **Never.** No `--fix`; a tool rewriting safety-case handling code unattended is not a trade this project makes |
| Tool version | Pinned. The baseline records it, CI installs exactly that from PyPI, and `--check` refuses to compare across a mismatch. Comparing two versions cost a phantom failure and a missed finding before [#317](https://github.com/lasrod/assurance-forge/issues/317) |
| Known gap | Windows only, so `#ifndef _WIN32` branches are unanalyzed |

The selection principle is that a check earns its place by finding defects
*here*. Enabling everything and suppressing the fallout produces a baseline
that is mostly noise, which is a baseline nobody reads.

## Sanitizers

`AF_SANITIZE` instruments the whole build. The `Sanitizers` workflow runs
AddressSanitizer and UndefinedBehaviorSanitizer over the full test suite on
Linux with GCC 14 — the compiler Coverage already pins, and the one known to
build this project. The job's first run used Clang 18 and could not compile the
codebase at all (`no template named 'expected' in namespace 'std'`, a C++23
library feature the GCC build compiles happily). Sanitizing with a compiler that
cannot build the project measures nothing, so the preference for Clang's
symbolized traces gave way to the compiler that works.

```bash
# Both compilers, not just CXX: the project enables C and C++, and leaving
# CMAKE_C_COMPILER at the default mixes toolchains — which is the one thing
# global instrumentation exists to avoid.
cmake -B build-asan -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
  -DAF_SANITIZE=address,undefined
cmake --build build-asan
ctest --test-dir build-asan --output-on-failure
```

| Aspect | Position |
|---|---|
| Scope | **Global**, deliberately — see below |
| Compilers | GCC (CI uses 14). Clang 18 cannot build this codebase — see above. **MSVC is refused**, with the reason — see below |
| Failure mode | `-fno-sanitize-recover=all`, so a finding aborts |
| When | Push to `main`, weekly, and on demand — **not** on pull requests |

**MSVC is refused rather than half-supported.** It has no UndefinedBehaviorSanitizer
at all, and its `/fsanitize=address` needs the optional *C++ AddressSanitizer*
Visual Studio component: without it the build compiles and then dies at link
with `cannot open file clang_rt.asan_dynamic_runtime_thunk-x86_64.lib`. That was
measured on this repository's own toolchain, not assumed. Nothing in CI
exercises it, so supporting it would be a claim nobody has tested — and a
configure-time refusal naming the reason beats a link error twenty minutes in.

**Global, unlike the warning policy.** A warning in hello_imgui is not ours to
fix, so warnings are per-target. Sanitizers are the opposite: memory allocated
by uninstrumented code and freed by instrumented code is how ASan produces
results that are wrong in both directions. They only tell the truth when
everything in the process is built the same way.

**`-fno-sanitize-recover=all` is the load-bearing flag.** UBSan's default is to
print a diagnostic and carry on, which exits zero — the job would go green while
reporting undefined behaviour. Aborting is what makes this a check rather than a
log.

### What the first run found

1,231 of 1,232 tests pass under ASan and UBSan. The one failure is a genuine
undefined-behaviour report, and it is not a test artifact:

```
imgui_widgets.cpp:7219:32: runtime error: shift exponent 11999 is too large
                          for 32-bit type 'int'
  #1 RenderTreeNode          src/ui/tree_view.cpp:323
  #2 ui::ShowTreeViewPanel   src/ui/tree_view.cpp:363
```

`TreePop` decrements `TreeDepth` and then computes `1 << TreeDepth` on a signed
`int`, so popping the innermost of *N* nested levels shifts by *N−1*. On a
32-bit `int` that is undefined once the exponent reaches 32 — **from 33 levels
deep**. The 12,000-node test only made it easy to see: it reported exponent
11999, which is 12000 − 1 and confirms the relationship.

`ShowTreeViewPanel` pushes one ImGui tree level per level of the user's
argument, so a large safety case reaches this too — it does not need a
synthetic tree.

The defect is in a submodule this project does not own, and the fix on our side
is a UI decision about how a very deep argument should render. It is tracked in
[#312](https://github.com/lasrod/assurance-forge/issues/312), and that one test
is excluded from the sanitizer run **by name, with the issue in the comment**,
so the exclusion cannot quietly become permanent. A parked finding is not a
clean bill.

**Not on pull requests**, for the reason [Coverage](../COVERAGE.md) gives for
itself: the sanitizer build shares no ccache with the ordinary one (every flag
differs, so every object is a miss) and ASan roughly doubles the test runtime.
That is a long time to add to the PR loop for a job whose value is finding
latent defects rather than reviewing a diff. A failure on `main` names a real
defect instead of blocking someone's branch.

## Repository gates

The mechanical enforcement points, gathered in one place. Which page is
canonical for each policy the gates protect is the
[documentation map](../documentation-map.md)'s to say.

The repository gates run under `ctest` (`ctest -L gate` runs the no-build
subset in about a second):

| Gate | Fails when |
|---|---|
| `i18n_catalog_check` | A source msgid is missing from the `.po`, the committed `.mo` is stale, or a translation carries a printf specifier |
| `sacm_matrix_check` | A `verified` conformance row has no ID-bearing test, a test names a requirement that does not exist, or a cited path moved |
| `gsn_matrix_check` | GSN taxonomy, statuses, or cited evidence drift |
| `feature_matrix_check` | A `supported` capability row cites no existing test, or the exported JSON is stale |
| `no_committed_artifacts_check` | A build log, test output, or scratch file is tracked by git |
| `documentation_check` | A broken internal link, an unreachable page, an unmarked generated doc, or an architecture page missing a subsystem |
| `layer_gate_negative_check` | The layer gate stops rejecting a synthetic violation — a self-test of the only mechanical guard on the architecture |
| `verification_index_check` | The verification index no longer matches the records' front matter |
| `evidence_package_check` | The release evidence-package generator no longer works against the current checkout |
| `agent_definition_check` | A generated agent definition under `.claude/agents/` or `.codex/agents/` was hand-edited or diverges from `.agents/agents/` |
| `ctest_label_check` | A test carries no label, a malformed one, or a `conformance` label that does not match an ID-bearing test name |

More controls run outside `ctest`:

| Control | Where | What it rejects |
|---|---|---|
| Layer dependency gate | Configure time, `cmake/check_layer_gates.cmake` | An `#include` that crosses a layer boundary |
| Warnings as errors | CI, all three platforms | Any new compiler warning — see [above](#warnings-are-errors-in-ci-not-locally) |
| clang-tidy ratchet | Its own CI job | A finding count that grew — see [static analysis](static-analysis.md) |
| clang-format | `.githooks/pre-commit` | Unformatted staged C++ — see [Formatting](#formatting) |

## Refactoring discipline

The cleanup programme's rules, from
[#293](https://github.com/lasrod/assurance-forge/issues/293). Nothing
mechanical rejects a pull request that breaks them; review does. They are
written down so a review argument can cite a rule rather than a taste.

1. **Mechanical and behavioural changes never share a PR.** A rename, move, or
   extraction that also changes behavior hides the behavior change behind the
   diff noise, which is where regressions in safety-case handling would live.
2. **Characterization tests come before risky change.** Before restructuring
   code that is complex or weakly understood — the
   [hotspot register](hotspot-register.md)'s top rows — add tests that record
   current behavior, land them first, and leave them untouched by the
   restructuring. A characterization test edited in the same PR as the change
   it guards proves nothing.
3. **Every refactoring PR states what it reduces** — a responsibility, a
   dependency, a complexity, a defect risk — in terms someone can check
   afterwards. "Cleanup" is not a rationale. A PR targeting a register row
   cites the row and its stated reduction criterion.
4. **Extract and narrow rather than relocate.** Moving complexity between
   files without reducing a responsibility or an interface moves the problem
   to where the next reader has not learned to look for it.
5. **No large naming-only PRs** unless they resolve a documented ambiguity —
   the `sacm/` include-prefix collision
   ([#341](https://github.com/lasrod/assurance-forge/issues/341)) being the
   standing example of one that would qualify.

### A worked example of rule 1

The `/W4` unreferenced-parameter warning pointed at a `ProcessChallenge`
parameter that was passed but never used. That omission was deliberate — a
counter relationship is a dialectic challenge, not structural support — but
pulling on it showed the mechanism behind it was dead.

`core::AssuranceTree` threaded a `wired_ids` set through five functions,
inserted into it in six places, and **never read it**. Orphan collection tested
`node->parent == nullptr` instead, and the "already wired" check tested
`child->parent`. The set was superseded state: whatever it once decided was
already decided by the parent pointer, and nothing kept the two in agreement.

Removing it touched tree building, so it was done on its own terms in
[#303](https://github.com/lasrod/assurance-forge/issues/303) rather than inside
a change about warning levels. The whole suite passed unchanged afterwards, with
no test edited to accommodate the removal — which is what distinguishes deleting
dead state from changing behaviour. Had a test needed adjusting, the set was
being read after all and the removal would have been wrong.

## Hotspots

Ranked in the [hotspot register](hotspot-register.md), by measured size, churn,
fan-in and domain risk together rather than any alone. The register states, for
each entry, what a reduction would observably look like — the criterion rule 3
above asks a refactoring PR to cite.

## Not yet enforced

Listed rather than omitted, because a gap nobody has written down reads as a
gap nobody has. Each row states the intended shape, so the follow-up issue that
picks it up has a spec to point at.

| Control | State | Intended shape when picked up |
|---|---|---|
| cppcheck | Not configured | A second opinion beside clang-tidy under the same arrangement: report mode against a committed, version-pinned baseline first, ratchet after triage. |
| Cyclomatic / cognitive complexity | Not measured | Report-mode measurement (lizard, or clang-tidy's `readability-function-cognitive-complexity`) feeding the [hotspot register](hotspot-register.md)'s ranking. No gate until a baseline exists. |
| Duplicated-code detection | Not measured | A clone detector in report mode first. Turning warnings on found six duplicated helpers in one file pair by accident, so there is reason to expect signal. |
| Dead code across translation units | Compiler warnings only | `-Wunused-function` and its MSVC equivalents catch file-local dead code; cross-TU dead code needs linker-assisted or dedicated tooling. |
| Formatting beyond C++ | Not configured | The same hook-first arrangement as clang-format: one pinned tool per language for CMake, Python, Markdown, YAML and JSON, applied on commit, no CI gate. |
| Fuzzing | Not configured | Tracked in [#292](https://github.com/lasrod/assurance-forge/issues/292). |
| Per-layer include roots | Open | An undeclared cross-layer include fails to compile — [#340](https://github.com/lasrod/assurance-forge/issues/340). |
