# Kitchen Blender — proposed deletion

The accepted Kitchen Blender safety case with one synthetic draft group that
proposes deleting `G10`. Its accepted `arguments/main.sacm` is byte-identical to
the clean `kitchen-blender` baseline.

Open `projects/kitchen-blender-deletion-draft/af.proj`, then open
`arguments/main.sacm`. Use this scenario to verify that a proposed deletion is
visibly distinct, its dependent relationships are disclosed, discard leaves the
accepted case intact, and promotion removes only the reviewed content.

Only the documented synthetic `workspace.json` is tracked. Reset after a manual
test from the parent repository:

```bash
git -C examples restore projects/kitchen-blender-deletion-draft
git -C examples clean -fdX -- projects/kitchen-blender-deletion-draft
```
