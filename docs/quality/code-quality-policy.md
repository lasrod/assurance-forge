# Code quality policy

What is enforced mechanically, what is only measured, and what is not checked at
all. Companion to the [repository quality baseline](repository-baseline.md),
which records the numbers at a point in time; this page records the rules.

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
| Enabled set | 90 findings. The four families with no exclusions give 4,784; nine excluded checks account for 4,694 of them (98%), each recorded with its count and reason |
| Enforcement | `run_clang_tidy.py --check` as its own CI job: changed `.cpp` files on a pull request, the full sweep on `main` |
| Incomplete runs | A translation unit clang-tidy cannot compile aborts the run. It contributes no findings, so continuing would record unanalyzed code as clean |
| Auto-fixing | **Never.** No `--fix`; a tool rewriting safety-case handling code unattended is not a trade this project makes |
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

**Not on pull requests**, for the reason [Coverage](../COVERAGE.md) gives for
itself: the sanitizer build shares no ccache with the ordinary one (every flag
differs, so every object is a miss) and ASan roughly doubles the test runtime.
That is a long time to add to the PR loop for a job whose value is finding
latent defects rather than reviewing a diff. A failure on `main` names a real
defect instead of blocking someone's branch.

## What is not enforced

Listed rather than omitted, because a gap nobody has written down reads as a
gap nobody has.

| Control | State |
|---|---|
| Static analysis (cppcheck) | **Not configured.** clang-tidy is — see [static analysis](static-analysis.md). A second opinion would catch what clang-tidy misses; no baseline exists for one. |
| Sanitizers (ASan, UBSan) | **Configured** — see [Sanitizers](#sanitizers) below. Not on pull requests. |
| Fuzzing | **Not configured.** Tracked in [#292](https://github.com/lasrod/assurance-forge/issues/292). |
| Cyclomatic / cognitive complexity | **Not measured.** No tool configured. |
| Duplicated-code detection | **Not measured.** |
| Formatting | `clang-format` on commit via `.githooks/pre-commit`. No CI gate. |
| Per-subsystem CMake dependency narrowing | **Open.** See [layers and ownership](../architecture/layers-and-ownership.md). |

## Hotspots

From the [baseline](repository-baseline.md), ranked by size and change
frequency together rather than either alone. Neither is a defect count; they
rank where cleanup is most likely to pay for itself.

| Area | Signal |
|---|---|
| `src/app/app_runtime.*` | Leads both lists — 62 and 48 commits, 1,271 lines |
| `src/app/app_runtime_project.cpp` | Near the top of both — 43 commits, 1,973 lines |
| `libs/sacm/src/io/xmi_reader.cpp` | Largest file at 2,299 lines; also the shadowing cluster above |
| `src/core/audit/event_replayer.cpp` | 1,979 lines |
| `src/core` overall | A third of production code, under a standing instruction to keep it small |

### Found by turning warnings on

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
