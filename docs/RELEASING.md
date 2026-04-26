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

The release zip is named `assurance-forge.<tag>.zip` and contains:

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

## Platform support

Releases are currently **Windows x64 only**, built with the `Visual Studio 17 2022` generator on GitHub-hosted `windows-latest` runners. Cross-platform release artifacts are tracked separately on the roadmap.
