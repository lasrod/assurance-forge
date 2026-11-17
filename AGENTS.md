# AGENTS.md

Assurance Forge is a tool for safety case engineering that runs as a desktop
application and as a web application: GSN for the user, SACM 2.3 XMI
underneath. It is being rebuilt in Rust; almost nothing is built yet.

Read first: [`ARCHITECTURE.md`](ARCHITECTURE.md) (shape and rules) and
[`docs/roadmap/rust-rebuild-phase-0.md`](docs/roadmap/rust-rebuild-phase-0.md)
(what was decided and what comes next).

## Commands

```bash
cargo fmt --all -- --check
cargo clippy --workspace --all-targets -- -D warnings
cargo test --workspace
cargo build -p af-gsn --target wasm32-unknown-unknown
```

Test only what you changed while working (`cargo test -p <crate>`); run the
workspace before pushing.

## Rules that are easy to break

- **Desktop and web.** Every feature works in the desktop application (Tauri,
  `af-desktop`) and in the web application (the core as WebAssembly,
  `af-wasm`). Only those two shells differ; the interface and the core are
  the same code.
- **One model.** The libSACM `Document` is the only model. Never add a second,
  editable representation of the argument. Views are derived and read-only;
  every change is a SACM operation.
- **Pure crates do no I/O.** Everything down to `af-api` must build for
  `wasm32-unknown-unknown`: no file system, network, threads or clock. Those come
  in through traits implemented by the shells.
- **`af-review` and `af-ai` never depend on each other.**
- **The core sends codes, not sentences.** Text for users is produced and
  translated in the frontend.
- **Never invent a GSN to SACM mapping.** Mappings are recorded with their
  evidence in `docs/standards/sacm-gsn-mapping.md`. Load the `gsn-expert` skill
  before GSN work.
- **Never silently change an argument.** Reading and writing a file must
  preserve its meaning; a change that cannot be made correctly is refused.
- **No data leaves the machine without the user's explicit consent.**

## Working here

- Always work on a branch and merge through a pull request. Never commit to
  `main` directly.
- Keep crates small and isolated. Add a crate when it can be built and tested
  without the rest.
- Changes to reading, writing, GSN to SACM mapping or edit commands need tests.
- Interface changes are checked in the running application, not only by tests.
- `ARCHITECTURE.md` is the living description. Update it in the same change
  that alters the shape of the code, including its "Built" column.
- Do not edit `external/` or `examples/`; they are submodules.
- The C++ application is on the `cpp` branch. Use it as a reference for
  behaviour, not as code to port line by line.

`CLAUDE.md` carries the same instructions for Claude Code. A change to one goes
into both.
