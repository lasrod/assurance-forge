<!--
Keep this short. A reviewer should be able to tell what changed and why
without reading the diff first.
-->

## What and why

<!-- What this changes, and the problem it solves. Link the issue. -->

## Verification

<!--
What you ran, and what it said. Name the tests. For an interface change, say
what you checked in the running application.
-->

- [ ] `cargo fmt`, `cargo clippy` and `cargo test --workspace` pass
- [ ] New or changed behaviour has a test
- [ ] `ARCHITECTURE.md` is updated if the shape of the code changed
- [ ] For a change to the interface or the layout: `pnpm --dir ui verify` passed, and its summary and before and after images are here
