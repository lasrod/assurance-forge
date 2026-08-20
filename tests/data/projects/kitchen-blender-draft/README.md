# Kitchen Blender — active working draft (test fixture)

The accepted Kitchen Blender safety case with a synthetic, unaccepted working
draft from three sources. Its accepted `arguments/main.sacm` is byte-identical to
the clean `kitchen-blender` baseline.

Read by `tests/test_draft_example_project.cpp`, which copies this directory into
a temporary directory per test. **To drive the scenario by hand, open
`examples/projects/kitchen-blender-draft/af.proj`** — that copy is there to be
opened and reset; this one is a test input.

The draft contains:

- an MCP-authored `G7` claim and support relationship under `G5`;
- an SCCG AI edit to `G1`;
- human edits to `G1` and `C1`.

This exercises the `NEW`, `EDIT`, and `MULTIPLE CHANGES` presentation as well as
selective promotion, **Accept all**, and **Discard draft**. Only the synthetic
`.af/drafts/arguments_main.sacm/workspace.json` is committed. All generated audit
events, backups, snapshots, and caches remain ignored.
