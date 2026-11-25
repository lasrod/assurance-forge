# Assurance Forge

Assurance Forge is an open-source tool for building, reviewing and navigating
safety cases. Engineers work in Goal Structuring Notation (GSN); the case is
stored as standard SACM 2.3 XMI, so it is never locked into the tool.

## Status

**Assurance Forge is being rebuilt in Rust, and `main` is where that happens.**
The rebuild opens a SACM 2.3 file and shows its argument as a GSN diagram, in
a desktop application and in the browser. Elements can be added and removed
and their text edited, with undo and redo, and the case saved back to SACM 2.3
XMI, with a recovery copy kept while changes are not saved.

| | |
|---|---|
| This branch | The Rust rebuild: a viewer and editor, with saving. See [`ARCHITECTURE.md`](ARCHITECTURE.md) for what is built and the [plan](docs/roadmap/rust-rebuild-phase-0.md) for what comes. |
| A tool you can use today | The C++ application: download [release 0.3.0-alpha.3](https://github.com/lasrod/assurance-forge/releases/tag/0.3.0-alpha.3), or read its source and documentation on the [`cpp` branch](https://github.com/lasrod/assurance-forge/tree/cpp). It is alpha software. |
| Licence | [MIT](LICENSE.md) |

## What the tool does

- **GSN on top of SACM.** Goals, strategies, solutions, context, assumptions and
  justifications, with patterns, modules, assurance claim points and challenges,
  read from and written to SACM 2.3 XMI.
- **Checks as you work.** GSN well-formedness, circular arguments, and the
  [Safety Case Core Guidelines](https://github.com/lasrod/safety-case-core-guidelines).
- **Registers and terminology.** Evidence and CSE registers, and a controlled
  vocabulary for each case.
- **AI on your terms.** Review with your own provider key, and an MCP server so
  your own assistant can read a case and propose changes. Nothing leaves your
  machine without your consent, and an AI never changes the argument by itself.
- **Local, on the desktop or in the browser.** A standalone desktop
  application, and the same tool as a web application. Either opens a file,
  edits it and saves it, and the case stays on your machine.

These describe the tool as a whole. So far the rebuild reads a case, draws it,
edits its core elements and saves it, on the desktop and in the browser;
patterns, modules and assurance claim points are not built yet. The
[plan](docs/roadmap/rust-rebuild-phase-0.md) says which of the rest arrive in
which release.

## How it is built

- A Rust core on [libSACM](https://github.com/lasrod/libSACM), the SACM 2.3
  library.
- A React interface, packaged as a desktop application with Tauri, and run in
  the browser on the core compiled to WebAssembly.

[`ARCHITECTURE.md`](ARCHITECTURE.md) describes the shape of the code and the
rules it follows.

## Building and starting it

You need Rust stable (`rust-toolchain.toml` selects it, with the WebAssembly
target). On Linux, the desktop application, a member of the workspace, also
needs WebKitGTK and the other
[prerequisites of Tauri](https://tauri.app/start/prerequisites/); without
them, leave it out with `cargo test --workspace --exclude af-desktop`.

```bash
git clone --recurse-submodules https://github.com/lasrod/assurance-forge.git
cd assurance-forge
cargo test --workspace
```

### Without the interface

The interface in `ui/` is built with DATUM, a design system that is not
public, so the application cannot be built from this repository alone. The
core can: the layout gallery reads the sample cases in `testdata/sacm/`, lays
each out and writes it as an SVG, with a table of what the geometry checks
found, into a folder beside the repository.

```bash
cargo run --release -p af-layout --example gallery -- ../gallery-out
```

### With the interface

This needs a checkout of DATUM, beside this repository as `../datum-ui` or
named by `DATUM_DIR`, and Node.js, pnpm 10 and wasm-pack
(`cargo install wasm-pack`).

```bash
pnpm --dir ui datum      # pack DATUM into ui/vendor/, which git ignores
pnpm --dir ui install
pnpm --dir ui wasm       # build the core as WebAssembly for the browser
```

**In the browser.** Start the development server and open
<http://localhost:5183>. Open a case from the welcome page, or name a sample:
<http://localhost:5183/?sample=sacm_example_core_argument.sacm.xmi>.

```bash
pnpm --dir ui dev
```

**On the desktop.** A debug build of the desktop application loads the
interface from the development server, so leave `pnpm --dir ui dev` running.
The file is optional.

```bash
cargo run -p af-desktop -- testdata/sacm/sacm_example_core_argument.sacm.xmi
```

A release build embeds the interface and runs on its own:

```bash
pnpm --dir ui build
cargo build -p af-desktop --release --features custom-protocol
target/release/af-desktop testdata/sacm/sacm_example_core_argument.sacm.xmi
```

The commands are written for a POSIX shell or PowerShell; in `cmd.exe` the
last is `target\release\af-desktop.exe` with the same argument.

`CLAUDE.md` lists the checks to run before a pull request.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md). Security reports go through
[SECURITY.md](SECURITY.md); questions through [SUPPORT.md](SUPPORT.md).
