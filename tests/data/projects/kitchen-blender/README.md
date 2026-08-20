# Kitchen Blender

A broad demonstration safety case for a domestic kitchen blender. It is useful
for exercising:

- a multi-level GSN argument;
- contexts, assumptions, strategies, goals, and solutions;
- terminology and multilingual compatibility content;
- automatic layout and SVG export;
- SACM import, validation, save, and reopen workflows.

## Open

In Assurance Forge, select **File → Open Project** and open:

```text
projects/kitchen-blender/af.proj
```

The SACM library accepts and semantically round-trips this long-lived demo
project. It intentionally retains multilingual compatibility content accumulated
while the application and its SACM mapping evolved. The current validator reports
legacy description-multiplicity warnings; those are useful migration regression
signals rather than evidence that the project failed to load.

This directory is the accepted baseline for every Kitchen Blender scenario.
Scenario tests assert that their `arguments/main.sacm` bytes remain identical to
this file, so proposed work cannot leak into the SACM source of truth.
