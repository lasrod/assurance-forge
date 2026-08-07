# Running tests

The suite is 1,231 CTest tests and takes about 40 seconds in Release. This page
is about running less than all of it, and about what the labels mean.

## The whole suite

```bash
cmake --build --preset release
ctest --test-dir build -C Release
```

CI runs Debug on Windows, Linux and macOS. A Release-only local pass is a good
proxy, not a guarantee — see
[code quality policy](../quality/code-quality-policy.md#checking-other-compilers-diagnostics-without-waiting-for-ci).

## Subsets

Every test carries at least one label, and `ctest -L` selects by them. **`-L`
matches a regular expression, not an exact label**, which is what lets a single
compound label such as `app.conformance` answer to both `-L app` and
`-L conformance`.

| Label | Tests | What it selects |
|---|---:|---|
| `app` | 1,109 | The Assurance Forge suite: core, app, ui, ai, parser, adapters |
| `conformance` | 226 | Evidence for a numbered requirement in the SACM 2.3 or GSN v3 matrix |
| `library` | 112 | `libs/sacm`, the reusable SACM 2.3 library, plus its CLI |
| `gate` | 8 | Repository checks: catalogues, matrices, documentation, artifacts. Need no build |
| `build` | 2 | Checks a build output rather than the repository, so it does need one |
| `cli` | 4 | The `sacm_cli` executable driven as a process |
| `contract` | 2 | The MCP stdio wire protocol, driven through real pipes |
| `slow` | 2 | Anything that starts a process |

```bash
# Before pushing: the repository gates. About two seconds, and no build
# needed -- they read the repository, not its output.
ctest --test-dir build -C Release -L gate

# Working on the SACM library.
ctest --test-dir build -C Release -L library

# Everything that backs a compliance claim.
ctest --test-dir build -C Release -L conformance

# Everything except the process-launching tests.
ctest --test-dir build -C Release -LE slow
```

Labels compose with `-R` for a name pattern:

```bash
# One requirement's evidence.
ctest --test-dir build -C Release -R SACM23_LIB_002
```

## What `conformance` means

Exactly one thing: **the test's name embeds a requirement id** — `SACM23_RT_001`,
`GSN3_CORE_009`, and so on. Nothing about the label is editorial.

That rule is enforced in both directions by the `ctest_label_check` gate. A test
whose name carries an id and lacks the label means the evidence set is
incomplete; a labelled test with no id means it is padded. Four `sacm_cli` tests
are labelled without an id in the name and are listed by name in the gate with
the reason, because they are `add_test()` invocations of a binary rather than
gtest cases.

This is the same rule `sacm_matrix_check` enforces from the other side, where a
matrix row that claims `verified` must cite an ID-bearing test.

## What the labels deliberately do not say

There is no `unit`, `integration` or `regression` label. Those distinctions are
real, but nothing in the repository currently records which test is which, and
1,200 tests cannot be classified accurately by guessing from their names. A
label that is wrong for a third of the suite is worse than an absent one,
because people would filter on it and quietly miss tests.

Assigning them is worth doing deliberately, per component, and is left open
under [#292](https://github.com/lasrod/assurance-forge/issues/292).

## Linking is still monolithic

`ctest -L library` *runs* only the library tests, but `sacm_tests` and `tests`
are still two large executables, so building either links most of what it
covers. Component-level targets — so that changing `core` does not relink the
UI suite — are a separate slice of #292 and are not done.

Two things do already hold: the library suite builds and runs without the
application (`cmake -S libs/sacm -B build-sacm` is a supported standalone
build), and the `gate` label needs no build at all.

## Test discovery

Tests are discovered at `ctest` time (`DISCOVERY_MODE PRE_TEST`), by running
each executable with `--gtest_list_tests`. It takes under half a second locally.

`DISCOVERY_TIMEOUT` is set to 60s rather than CMake's 5s default. A loaded
Windows CI runner overran the default once and reported

```
discover_tests failed to run command: ...
Process terminated due to timeout
```

which reads as a broken build rather than a busy machine.

## Adding a test

Nothing to do for labels: a new gtest case in an existing file inherits its
executable's labels, and picks up `conformance` automatically if its name
embeds a requirement id.

A new `add_test()` needs `LABELS` set explicitly, and `ctest_label_check` fails
if it does not — an unlabelled test is invisible to every `-L` selection above,
which is a quiet way to stop being run.

One trap when adding labels to `gtest_discover_tests`: **it cannot carry a
multi-value `LABELS` portably.** GoogleTest.cmake expands the property list
unquoted into `set_tests_properties()`, so `LABELS "a;b"` arrives as two
arguments and `PROPERTIES` silently keeps only `LABELS=a`. Escaping it as
`"a\;b"` is no better — that produced two labels on CMake 4.3 and one combined
label `'a;b'` on the CI runners, so the fix was version-dependent and CI caught
it only because this gate existed.

Use **one compound label** instead, `app.conformance` rather than `app` plus
`conformance`. `ctest -L` matches labels as a regular expression, so `-L app`
and `-L conformance` both select it and nothing has to survive a CMake list
expansion. A direct `set_tests_properties()` is not re-expanded and can still
take a real list, which is why both forms appear in the build files.
