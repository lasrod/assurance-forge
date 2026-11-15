# Contributing

Assurance Forge is being rebuilt in Rust. Read the [README](README.md) for where
things stand and [`ARCHITECTURE.md`](ARCHITECTURE.md) for the shape of the code
before changing it.

## Principles

- **The user's argument is theirs.** SACM XMI is the source of truth. The tool
  must never silently change or reinterpret a safety argument, and a file that
  is read and written again must keep its meaning.
- **Nothing leaves the machine without consent.** AI and MCP features are
  explicit and user-controlled.
- **Claims are backed by tests.** A capability is described as supported only
  when a test shows it.
- **Small and fast.** Prefer a small crate with narrow dependencies. A change
  should be checkable in seconds locally.

## Before opening a pull request

```bash
cargo fmt --all -- --check
cargo clippy --workspace --all-targets -- -D warnings
cargo test --workspace
```

Changes to how a case is read, written, mapped between GSN and SACM, or edited
need tests. Changes to the interface should be checked in the running
application.

## Code style

- Rust 2024 edition, formatted with `rustfmt` and clean under `clippy`.
- No `unsafe`.
- Full words for names; abbreviations only for established domain terms (SACM,
  GSN, SCCG, ACP).
- Each crate's top-level documentation says what the crate owns and what it must
  not do.
- Text shown to users is produced in the frontend. The core returns codes and
  arguments.

## Pull requests

- One purpose per pull request.
- Say what changed for a user, and how you checked it.
- Do not edit files under `external/` or `examples/`; they are submodules.

## The C++ application

The previous implementation lives on the
[`cpp` branch](https://github.com/lasrod/assurance-forge/tree/cpp). It receives
no new features.
