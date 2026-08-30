# Export a diagram

**File → Export → GSN SVG**, or the export button in the toolbar. The file is
written into the project's `exports/` folder.

## What the SVG carries

- Core GSN nodes and `SupportedBy` / `InContextOf` / `Challenges` edges, laid
  out exactly as the canvas laid them out.
- Decorators: the undeveloped diamond, the pattern notation, ACP markers, and
  dashed open-arrow challenge edges.
- `gsn-*` CSS classes on nodes and edges, so an exported diagram can be
  restyled for a report without editing geometry.
- Evidence links: a node whose evidence has a recorded location is wrapped in an
  `<a href>` with an external-link glyph, so the diagram is clickable in a
  browser.

The canvas and the exporter are **separate renderers**. They are kept in step
deliberately, but a decorator added to one is not automatically in the other —
which is why the [capability matrix](../features/feature-matrix.md) records them
as separate rows.

## Languages

The export takes the language the canvas is showing. A secondary-language export
takes each field's translation with a per-field fallback to the primary, and the
language code goes into the file name — so a Japanese export cannot silently
overwrite the English one. Repeated exports are numbered rather than
overwritten.

## Before you send it to someone

Two things travel with the file:

- An **absolute** evidence location becomes a `file:` URL, which carries your
  local directory layout into a document you are about to hand over.
- A **project-relative** location is rebased onto the `exports/` folder, so it
  resolves next to the project and not on the recipient's machine.

Links are filtered on export: only `http`, `https`, `file` and `mailto` are
emitted, and anything else — a `javascript:` or `data:` location that arrived
inside an imported case — is dropped with a warning rather than shipped inside a
document other people will open.

## What is not exported

A report — a full safety case document in LaTeX or PDF — is not implemented. It
is on the [roadmap](../roadmap/public.md), and the matrix row (`AF-ENG-004`)
says `planned`, which means no code and no tests, not "nearly there".
