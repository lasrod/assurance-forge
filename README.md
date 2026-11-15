# Assurance Forge

Assurance Forge is an open-source tool for building, reviewing and navigating
safety cases. Engineers work in Goal Structuring Notation (GSN); the case is
stored as standard SACM 2.3 XMI, so it is never locked into the tool.

## Status

**Assurance Forge is being rebuilt in Rust, and `main` is where that happens.**
There is nothing to run here yet.

| | |
|---|---|
| This branch | The Rust rebuild, in its first phase. See the [plan](docs/roadmap/rust-rebuild-phase-0.md) and [`ARCHITECTURE.md`](ARCHITECTURE.md). |
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
- **Local.** A desktop application that opens a file, edits it and saves it.

The [plan](docs/roadmap/rust-rebuild-phase-0.md) says which of these arrive in
which release.

## How it is built

- A Rust core on [libSACM](https://github.com/lasrod/libSACM), the SACM 2.3
  library.
- A React interface, packaged as a desktop application with Tauri.

[`ARCHITECTURE.md`](ARCHITECTURE.md) describes the shape of the code and the
rules it follows.

## Building

```bash
git clone --recurse-submodules https://github.com/lasrod/assurance-forge.git
cd assurance-forge
cargo test --workspace
```

Rust stable is required; `rust-toolchain.toml` selects it.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md). Security reports go through
[SECURITY.md](SECURITY.md); questions through [SUPPORT.md](SUPPORT.md).
