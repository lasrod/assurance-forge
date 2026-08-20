# Kitchen Blender — test fixture

The accepted baseline the draft-workspace tests read.
`tests/test_draft_example_project.cpp` copies this directory into a temporary
directory for each test, because opening a project writes to it.

**To open a Kitchen Blender project by hand, use
`examples/projects/kitchen-blender/af.proj`.** That copy exists to be opened and
reset; this one is an input to the tests, and editing it changes what they
assert.

It covers:

- a multi-level GSN argument;
- contexts, assumptions, strategies, goals, and solutions;
- terminology and multilingual compatibility content;
- automatic layout and SVG export;
- SACM import, validation, save, and reopen workflows.

The SACM library accepts and semantically round-trips this long-lived demo
project. It intentionally retains multilingual compatibility content accumulated
while the application and its SACM mapping evolved. The current validator reports
legacy description-multiplicity warnings; those are useful migration regression
signals rather than evidence that the project failed to load.

This directory is the accepted baseline for every Kitchen Blender scenario.
Scenario tests assert that their `arguments/main.sacm` bytes remain identical to
this file, so proposed work cannot leak into the SACM source of truth.

`af.proj` records a sha256 over those bytes, so anything that changes them --
including a CRLF checkout -- makes the project report itself modified outside the
tool. `.gitattributes` pins this tree to LF for that reason.
