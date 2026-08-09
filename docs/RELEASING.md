# Releasing

This project uses an automated GitHub Actions workflow (`.github/workflows/release.yml`) to build Windows binaries and publish releases.

## Tag conventions

Tags use semantic versioning **without a `v` prefix**:

- Stable: `<major>.<minor>.<patch>` — e.g. `0.2.0`, `1.0.0`
- Prerelease: `<major>.<minor>.<patch>-<label>` — e.g. `0.1.0-alpha.5`, `1.0.0-rc.1`

Tags containing `-` are automatically marked as **prerelease** by the workflow.

## Cutting a release

1. Make sure `main` is in the state you want to release.
2. Tag the commit and push the tag:

   ```bash
   git tag 0.2.0
   git push origin 0.2.0
   ```

3. The `Release` workflow builds the project, packages a zip, and creates a GitHub Release named `assurance-forge <tag>` with the zip attached.
4. Edit the Release on GitHub to add a description. The workflow leaves the body empty intentionally so the release notes can be written by hand.

The release zip is named `assurance-forge.<tag>-windows-x64.zip` and contains:

- `assurance-forge.exe`
- `data/` (sample SACM files)
- `README.md`
- `LICENSE.md`

## Experimental builds (no release)

To build a packaged zip without creating a GitHub Release, use **workflow_dispatch**:

1. Go to **Actions → Release → Run workflow** on GitHub.
2. Pick a branch and click *Run workflow*.
3. When the run finishes, download the zip from the run's *Artifacts* section.

`workflow_dispatch` builds use a `dev-<short-sha>` version string and never create a GitHub Release — even when run from the default branch.

> Note: `workflow_dispatch` only works for workflow files that exist on the repository's default branch. To run an experimental build from a feature branch, the workflow file must already be present on `main`.

## Conformance evidence package

Alongside the binaries, the Windows job generates and attaches
`assurance-forge.<tag>-evidence-package.zip` — the release-bound SACM 2.3
conformance evidence required by
[#295](https://github.com/lasrod/assurance-forge/issues/295): the frozen
conformance matrix and decision pages, requirement-to-test traceability, the
release build's machine-readable test results, the pinned normative-source
hashes, and a generated conformance statement naming the exact release. See
[the conformance statement page](sacm/sacm-conformance-statement.md) for what
the package means and `tools/sacm/generate_evidence_package.py` for how it is
built. To reproduce one locally:

```bash
python tools/sacm/generate_evidence_package.py --allow-missing-test-results
```

The `evidence_package_check` CTest gate runs the generator's `--check` mode on
every gate run, so a broken generator is caught before a release tag needs it.

## Release notes policy

**The GitHub Releases page is this project's changelog.** There is no
`CHANGELOG.md`. A hand-maintained changelog alongside hand-written release notes
gives two sources that drift, and the one people actually read is the one
attached to the download.

Every release note must state, in this order:

1. **What changed for users** — new capabilities, changed behaviour, fixes.
   Written so someone who has not read the commits can understand the effect.
2. **Anything affecting existing files** — a change to how a project or SACM
   file is read, written, or migrated. Say explicitly whether files written by
   an older version still load, and whether files written by this version load
   in an older one.
3. **Known limitations** introduced or still outstanding.
4. **The commit SHA** the release was built from.

A release that changes parsing, serialization, migration, audit or undo
behaviour **must** say so even when the change is an improvement. Someone
deciding whether to upgrade a tool holding their safety argument needs to know
that the file handling moved, not only that a bug was fixed.

Do not describe a release as conformant, certified, qualified or approved. State
what was implemented and what was tested, and link to the evidence. The
repository README's "Status and limitations" section is the reference for what
this project does and does not claim.

## Platform support

Release binaries are built on GitHub-hosted runners (`windows-latest`, `ubuntu-latest`, `macos-latest`); the Windows build uses the newest Visual Studio installed on the runner rather than a pinned generator, matching CI. The evidence package is generated on the Windows job.
