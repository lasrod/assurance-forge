# Assurance Forge

Assurance Forge is an open-source tool for building, reviewing and navigating
safety cases. Engineers work in Goal Structuring Notation (GSN); the case is
stored as standard SACM 2.3 XMI, so it is never locked into the tool.

## Status

**Assurance Forge is being rebuilt in Rust, and `main` is where that happens.**
The rebuild opens a SACM 2.3 file and shows its argument as a GSN diagram, in
a desktop application and in the browser, and elements can be added to it,
with undo and redo. It does not save yet.

| | |
|---|---|
| This branch | The Rust rebuild: a viewer, with editing begun. See [`ARCHITECTURE.md`](ARCHITECTURE.md) for what is built and the [plan](docs/roadmap/rust-rebuild-phase-0.md) for what comes. |
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

These describe the tool as a whole. The rebuild does the first so far: it reads
a case and draws it. The [plan](docs/roadmap/rust-rebuild-phase-0.md) says
which of the rest arrive in which release.

## How it is built

- A Rust core on [libSACM](https://github.com/lasrod/libSACM), the SACM 2.3
  library.
- A React interface, packaged as a desktop application with Tauri, and run in
  the browser on the core compiled to WebAssembly.

[`ARCHITECTURE.md`](ARCHITECTURE.md) describes the shape of the code and the
rules it follows.

## Building

```bash
git clone --recurse-submodules https://github.com/lasrod/assurance-forge.git
cd assurance-forge
cargo test --workspace
```

Rust stable is required; `rust-toolchain.toml` selects it. The interface in
`ui/` is built with a design system that is not public, so it cannot be built
from this repository alone; `CLAUDE.md` lists the commands for those who have
it.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md). Security reports go through
[SECURITY.md](SECURITY.md); questions through [SUPPORT.md](SUPPORT.md).
