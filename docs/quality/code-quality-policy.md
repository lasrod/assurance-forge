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
clang++ -std=c++23 -fsyntax-only -Wall -Wextra     -Wno-missing-field-initializers -D_CRT_SECURE_NO_WARNINGS     -Isrc -Ilibs/sacm/include -Ilibs/sacm/src     -Iexternal/hello_imgui/src -Iexternal/hello_imgui/external/imgui     -Iexternal/hello_imgui/external/imgui/backends     -Iexternal/hello_imgui/external/imgui/misc/cpp     -Iexternal/pugixml/src -Iexternal/picosha2     -Iexternal/nativefiledialog-extended/src/include     $(for d in build/_deps/*-src/include build/_deps/*-src/single_include; do echo -I"$d"; done)     <file>.cpp
```

**Use clang, not the MinGW GCC.** That distinction is the whole point of this
section. A GCC sweep on this machine reported all 254 first-party files clean
while CI's Linux GCC and macOS Clang both rejected `src/ui/theme.cpp` for two
unused variables — and it stayed silent under `-fsyntax-only`, `-c -O0` and
`-c -O1` alike, so it was the compiler build rather than the mode. Clang
reproduces CI's findings exactly, and parses all 254 files without needing any
to be skipped.

A local check that reports clean on code CI rejects is worse than no check: it
turns "I did not look" into "I looked and it was fine".

Sweep **`tests/` and `libs/sacm/tests/` too**, not just `src/`. The warning
policy covers the test targets, so a sweep that skips them checks less than the
build does — that gap let three `-Wrange-loop-construct` diagnostics reach CI.

Pass the same defines the build passes, at minimum `SACM_REPO_FIXTURES_DIR` and
`_CRT_SECURE_NO_WARNINGS`. Without the first, `test_metamodel_coverage.cpp`
compiles its `#ifndef` branch and reports a constant as unused that the real
build uses — deleting it on that advice would have broken the build.

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

## What is not enforced

Listed rather than omitted, because a gap nobody has written down reads as a
gap nobody has.

| Control | State |
|---|---|
| Static analysis (clang-tidy, cppcheck) | **Not configured.** No baseline exists yet. |
| Sanitizers (ASan, UBSan) | **Not configured.** Tracked in [#292](https://github.com/lasrod/assurance-forge/issues/292). |
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

`core::AssuranceTree` threads a `wired_ids` set through five functions. It is
inserted into in five places and **never read**: orphan collection uses
`node->parent == nullptr` instead, and the "already wired" check tests
`child->parent`. The set is superseded state that no longer affects anything.

The `/W4` unreferenced-parameter warning pointed at one symptom of this — a
`ProcessChallenge` parameter that was passed but unused. Removing the whole
mechanism touches tree building, so it is worth doing on its own terms rather
than inside a change about warning levels.
