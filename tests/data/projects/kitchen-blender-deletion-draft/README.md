# Kitchen Blender — proposed deletion (test fixture)

The accepted Kitchen Blender safety case with one synthetic draft group that
proposes deleting `G10`. Its accepted `arguments/main.sacm` is byte-identical to
the clean `kitchen-blender` baseline.

Read by `tests/test_draft_example_project.cpp`, which copies this directory into
a temporary directory for each test. **To drive the scenario by hand, open
`examples/projects/kitchen-blender-deletion-draft/af.proj`** — that copy is there
to be opened and reset; this one is a test input.

The scenario verifies that a proposed deletion is visibly distinct, that its
dependent relationships are disclosed, that discard leaves the accepted case
intact, and that promotion removes only the reviewed content. Only the documented
synthetic `workspace.json` is tracked.
