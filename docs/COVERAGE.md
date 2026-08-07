# Code Coverage

This project uses a dedicated GitHub Actions workflow
(`.github/workflows/coverage.yml`) to produce HTML code coverage
reports for the Linux build, using `gcovr` 8.x with GCC 14.

## Why GCC 14

Two reasons:

- `-fcondition-coverage` (MC/DC, "modified condition / decision
  coverage") was added in GCC 14. Branch coverage on its own counts
  every compiler-generated arc — exception edges, loop back-edges,
  destructor paths — which makes the headline number hard to
  interpret. MC/DC reports only the true source-level boolean
  conditions, which is closer to what a reader expects from "did the
  tests cover the logic".
- The `ubuntu-latest` GitHub-hosted runner pre-installs `g++-14`, so
  the toolchain bump only affects this single workflow. `ci.yml` and
  `release.yml` continue to use the default GCC 13 unchanged.

## Why several report views

The workflow produces four HTML reports as a single artifact (this section
described two of them for some time after the other two were added, which is
why it now names them all), plus the per-component figures below. The two that
carry the argument:

- `coverage_full/` — every translation unit under `src/`. Honest
  picture of the whole codebase, including UI panels and the
  application bootstrap layer that have no headless unit tests
  today.
- `coverage_core/` — `src/` minus `src/app/` and `src/ui/`. Focused
  view of the code paths that the unit tests actually exercise
  (parser, SACM, problems, AI client, GSN layout helper,
  localization).

Reporting only the full view drags the headline number down because
of code that cannot be tested headlessly with the current test
infrastructure. Reporting only the core view hides the gap that GUI
code has no automated coverage. Publishing both lets a reviewer see
both perspectives.

Two more are published alongside them: `coverage_logic/` (the headline logic
scope, defined in `gcovr-logic.cfg`) and `coverage_sacm/` (the SACM library
alone, because it is the surface every conformance claim rests on and a
combined number would let a library regression hide behind application
coverage).

## Per-component coverage, and the ratchet

Each of the four scopes above is an average, and an average is exactly what
hides a subsystem getting worse: a drop in `src/parser` disappears inside a
number dominated by `src/core`. The workflow therefore also reports **coverage
per component** — `libs/sacm`, `src/core`, `src/ui`, `src/app`, `src/parser`
and the rest — from a single machine-readable `gcovr --json` run.

```bash
python tools/quality/coverage_components.py --gcovr-json coverage.json
python tools/quality/coverage_components.py --gcovr-json coverage.json --check
```

**Each component is held to its own measured value**, recorded in
`docs/quality/coverage-baseline.json`. That is deliberate and is what
[#292](https://github.com/lasrod/assurance-forge/issues/292) asks for: a single
repository-wide floor would be satisfied by `libs/sacm` at 84% while `src/ui`
fell straight through it.

Three rules the check follows:

- **Only decreases fail.** A rise updates nothing automatically — the baseline
  is regenerated deliberately, so an improvement is a commit somebody made
  rather than a number that drifted upward on its own.
- **A component that vanishes from the report is a failure, not an absence.**
  That is what a build which stopped compiling a subsystem looks like, and it
  would otherwise read as "nothing to report here".
- **Generate or check, never both in one run.** Checking a baseline generated
  moments earlier is a gate that cannot fail. The workflow generates only when
  no baseline is committed, and says so.

Tolerance is 0.2 percentage points: enough to absorb a line moving between
files, not enough to absorb a subsystem losing tests.

## Why these gcovr flags

- `--filter 'src/'` — limit reporting to project source. Test files
  themselves are not measured (the GoogleTest macro expansions would
  add several thousand assertion branches to the denominator).
- `--exclude 'external/' --exclude '_deps/'` — third-party code
  pulled in via `add_subdirectory` and `FetchContent` is not measured.
- `--exclude 'build-coverage/'` — generated headers in the build
  directory are not measured.
- `--exclude-throw-branches` — drops the implicit branches GCC emits
  for every potentially-throwing call. These dominate the branch
  denominator without representing decisions written in the source.
- `--exclude-unreachable-branches` — drops branches the optimizer
  determined cannot be taken.
- `--gcov-executable gcov-14` — required because GCC 14 emits `.gcno`
  files in format B42, which `gcov-13` (the system default on Ubuntu
  24.04) cannot read.

## Running locally

Requires GCC 14. Ubuntu 24.04 ships it in the main repository; older
distributions may need a backport.

```bash
sudo apt-get install -y gcc-14 g++-14 pipx \
    libssl-dev libcurl4-openssl-dev \
    xorg-dev libgl1-mesa-dev libglu1-mesa-dev libgtk-3-dev
pipx install gcovr

cmake -B build-coverage \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_C_COMPILER=gcc-14 \
    -DCMAKE_CXX_COMPILER=g++-14 \
    -DHELLOIMGUI_DOWNLOAD_GLFW_IF_NEEDED=ON \
    -DENABLE_COVERAGE=ON
cmake --build build-coverage
ctest --test-dir build-coverage --output-on-failure

mkdir -p coverage_full
gcovr -r . build-coverage \
    --filter 'src/' \
    --exclude 'external/' --exclude 'build-coverage/' --exclude '_deps/' \
    --exclude-throw-branches --exclude-unreachable-branches \
    --gcov-executable gcov-14 \
    --html-details coverage_full/index.html \
    --print-summary
```

For the core scope view, add `--exclude 'src/app/' --exclude 'src/ui/'`
and write to `coverage_core/index.html`.

## CI artifact

Each Coverage workflow run uploads `coverage-reports.zip` containing
both views. Open `coverage_full/index.html` or
`coverage_core/index.html` in a browser to navigate the report.
