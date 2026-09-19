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

A Windows release carries two packages built from the same install layout:

- `assurance-forge.<tag>-windows-x64-setup.exe` — an Inno Setup installer. It
  installs per user into `%LOCALAPPDATA%\Programs\Assurance Forge` without
  administrator rights (an all-users install is offered to someone who can
  elevate), adds a Start-menu shortcut, and upgrades an earlier install in place.
  On uninstall it asks whether to keep the user's settings, defaulting to keep.
- `assurance-forge.<tag>-windows-x64.zip` — the same files, to unzip and run.

Both hold `assurance-forge.exe`, `assurance-forge-mcp.exe`, `assets/`, the SCCG
catalogue in `data/sccg/dist/`, the sample SACM files in `data/`, the Visual C++
runtime DLLs, `README.md` and `LICENSE.md`.

### Where the layout is defined

The Windows packages come from the `install()` rules in
[`cmake/packaging.cmake`](https://github.com/lasrod/assurance-forge/blob/main/cmake/packaging.cmake)
and CPack; a file the application needs at runtime is added there, not in the
workflow. Linux and macOS archives are still staged by hand in the workflow.

The workflow checks both Windows packages before publishing them, with
`tools/release/check_windows_package.py`: every file the application looks for
beside itself must be present, every DLL either executable imports must be part
of Windows or shipped in the package, and `assurance-forge-mcp.exe --version`
must start. The installer is installed silently, checked, and uninstalled. A
package that runs on the build machine proves little, because the build machine
has the Visual C++ runtime installed and a tester's machine may not.

### The installer's pages and look

`packaging/windows/` holds what CPack does not generate: `installer.iss` (the
components page, the AI-assistant tasks, all the wizard text in English and
Japanese, and the Finish page's launch, user-guide and release-notes boxes),
`installer_code.pas` (first install vs upgrade, MCP client registration, the
Finish text, and the keep-settings question on uninstall), and `art/`, the
wizard images at each DPI size.

The components page lists the application's built-in features ticked and
locked, so a new user sees what they are getting; **every feature named there
must be a `supported` row in the capability matrix.** The sample cases and the
MCP server are optional components; `cmake/packaging_project_config.cmake` ties
their files to them. When Claude Code or Codex is on PATH, the installer offers
(unticked) to register the MCP server with it at user scope, with no arguments.
It never touches an `assurance-forge` entry it did not create, records the ones
it did in `HKCU\Software\Assurance Forge\Installer`, and removes only those on
uninstall. Registration shares nothing by itself: the MCP consent gate in the
application still applies. The images are rendered from the application icon by
`tools/release/render_installer_art.py`; rerun it after changing the icon or the
theme colours. The [Setup] directives -- page flow, colours, light/dark mode --
are in `cmake/packaging.cmake`.

The script needs **Inno Setup 6.6 or later** and refuses to compile on an older
one. The release workflow pins 6.7.1 so a release's installer does not change
with the runner image; raise the pin deliberately, after building the installer
locally with the new version.

### Building the packages locally

With [Inno Setup 6](https://jrsoftware.org/isinfo.php) installed
(`winget install JRSoftware.InnoSetup`):

```bash
cmake --preset default -DAF_PACKAGE_VERSION=0.2.0-alpha.3
cmake --build --preset release
cpack --config build/CPackConfig.cmake -C Release -B build/package
python tools/release/check_windows_package.py <unzipped-or-installed-dir> --run
```

`AF_PACKAGE_VERSION` defaults to `0.0.0-dev`. It is written into the package
names and the installer's displayed version; the Windows file-version resource
takes only its numeric `major.minor.patch` part.

### The packages are not code-signed

Windows SmartScreen warns on first run (*More info → Run anyway*). Signing is
deferred while the project is in alpha; the free SignPath Foundation programme
for open-source projects is the likely route when it is added. The installer's
`AppId` in `cmake/packaging.cmake` must never change: it is how a newer
installer finds and upgrades the installed copy.

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
