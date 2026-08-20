# Kitchen Blender — active working draft

The accepted Kitchen Blender safety case with a synthetic, unaccepted working
draft from three sources. Its accepted `arguments/main.sacm` is byte-identical to
the clean `kitchen-blender` baseline.

Open `projects/kitchen-blender-draft/af.proj`, then open
`arguments/main.sacm`. The draft contains:

- an MCP-authored `G7` claim and support relationship under `G5`;
- an SCCG AI edit to `G1`;
- human edits to `G1` and `C1`.

This exercises the `NEW`, `EDIT`, and `MULTIPLE CHANGES` presentation as well as
selective promotion, **Accept all**, and **Discard draft**. Only the synthetic
`.af/drafts/arguments_main.sacm/workspace.json` is committed. All generated audit
events, backups, snapshots, and caches remain ignored.

Reset after a manual test from the parent repository:

```bash
git -C examples restore projects/kitchen-blender-draft
git -C examples clean -fdX -- projects/kitchen-blender-draft
```
