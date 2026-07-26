# Implementing GSN features in Assurance Forge

Where GSN actually lives, and the seams a change has to pass through. Paths are
current as of this file's writing — verify before relying on one.

## The two renderers

This is the fact that most often causes an incomplete GSN feature.

| | Interactive canvas | SVG export |
|---|---|---|
| Model | `ui::gsn::CanvasElement` / `LayoutNode` (`src/ui/gsn/gsn_model.h`) | `export_gsn::GsnNode` / `GsnEdge` (`src/export/gsn_diagram.h`) |
| Node vocabulary | `ElementRole` — 9 roles | `GsnNodeKind` — 6 kinds |
| Edge vocabulary | SupportedBy, InContextOf, Challenges | `GsnEdgeKind` — SupportedBy, InContextOf, Challenges |
| Decorators | undeveloped, ACP | undeveloped, ACP |
| Layout | `src/ui/gsn/gsn_layout.cpp`, `src/core/gsn_layout.cpp` | `src/export/gsn_svg_layout.cpp` (reuses `core::LayoutGsnGraph`) |
| Drawing | `src/ui/gsn/gsn_shapes.cpp`, `gsn_canvas_renderer.cpp`, `gsn_edge_renderer.cpp` | `src/export/gsn_svg_exporter.cpp`, `svg_writer.cpp` |

They share no drawing code. Anything added to one is absent from the other until
added separately, and the failure is silent — the export simply omits it.

This has already cost the project once (AF-ENG-015): the SVG projection ignored
`is_counter`, so a challenge relationship was exported as an ordinary
`SupportedBy` edge and **counter-evidence was drawn as support for the claim it
attacks**. Note the shape of that bug — it was not a missing feature but a wrong
one, and nothing in the diagram looked broken.

**When adding anything visible, decide explicitly whether it goes in both — and
if not, say so in the matrix row rather than leaving it implied.**

## Adding a GSN element kind

Follow the layering rule: `core`/`parser`/`sacm` never include ImGui or `app`
headers, and `ui` never depends on `app`.

1. **Library** — `libs/sacm/src/io/name_tables.cpp` maps GSN `xsi:type` names to
   SACM classes on import. All three GSN namespaces are accepted; watch the
   inverted version numbering (`scsc.acwg.gsn/2.0` is *current*).
2. **Mapping decision** — add a row to `docs/sacm/sacm-gsn-mapping.md` with its
   evidence class (`[M]`/`[N]`/`[G]`). No code before the row.
3. **Model** — `src/core/sacm_model.h` (`SacmElement.type` is a lowercased
   local-name string).
4. **Creation** — `src/core/element_factory.cpp`.
5. **Tree and editing** — `src/core/assurance_tree.cpp`, `src/core/tree_editing.cpp`.
6. **Canvas role** — `ElementRole` in `src/ui/gsn/gsn_model.h`, mapped in
   `MapType()` in `src/ui/gsn/gsn_adapter.cpp`. `ElementRole` is switched on in
   `gsn_layout.cpp` and `gsn_canvas_renderer.cpp` — both need a case.
7. **Shape** — `src/ui/gsn/gsn_shapes.cpp`. Colors come from `ui::GetTheme()` or
   a semantic helper, never a local `ImVec4`.
8. **Export** — `GsnNodeKind` in `src/export/gsn_diagram.h`, plus
   `gsn_projection.cpp`, `gsn_svg_layout.cpp`, `gsn_svg_exporter.cpp`.
9. **Strings** — every user-visible literal through `AF_TR(...)`, then
   `python tools/i18n/regenerate_ja_po.py`.
10. **Tests** — parsing, serialization, model mutation and layout changes all
    require them (`tests/test_element_factory.cpp`, `test_layout.cpp`,
    `test_tree_editing.cpp`, `test_gsn_svg_exporter.cpp`).
11. **Matrices** — a row in `docs/features/feature-matrix.md`, and a row in
    `docs/sacm/sacm-conformance-matrix.md` if a SACM obligation is involved.
    Then `python tools/features/export_feature_matrix.py` and run both checkers.

## Adding a decorator

Decorators are usually cheaper: a flag on the element, a badge at draw time.

- Flag on `core::SacmElement` (`undeveloped` is the existing example) or, for
  GSN attributes SACM has no feature for, a reserved TaggedValue — the pattern
  already used for `sacm.import.name`.
- Carry it through `CanvasElement` → `LayoutNode` in `gsn_adapter.cpp`; layout
  may need to reserve space.
- Draw in `src/ui/gsn/gsn_badges.cpp` or `gsn_shapes.cpp`.
- **And in `src/export/`**, or record the gap.

## Adding a relationship kind

The dialectic Challenge edge is the worked example — read it before designing a
new one. `LayoutNode` carries `is_counter_source`, `challenge_target_id`,
`challenge_target_is_relationship`, `challenge_relationship_id` and
`challenge_anchor_id`, because a challenge can target a *relationship* as well as
an element and can itself be challenged. Rendering is in
`src/ui/gsn/gsn_edge_renderer.cpp`; the layout reserves a side stack and uses
`child_edge_drop` to clear it.

Be explicit about direction. GSN relationships run conclusion→premise and the
SACM equivalents run the other way; the reader swaps on import.

## Where the argument-quality path lives

- Problems: `src/core/problems/` → `src/ui/panels/problems_panel.cpp`.
- Guidelines: `src/parser/sccg_dist_parser.cpp`, `guidelines_parser.cpp`, from
  the `external/safety-case-core-guidelines` submodule, copied to
  `data/sccg.full.yaml` at build time.
- AI review: `src/ai/ai_claim_review.cpp`; proposals and patches in
  `src/core/reviews/`.

Prefer a new SCCG guideline over a hardcoded check — it is data, it is
translatable, and it is reviewable by the safety community rather than only by
the compiler.

## Things that will bite you

- **`ElementRole::Other` is a silent fallback.** `MapType()` returns it for any
  unmapped type string, so a new element kind renders as a nondescript box with
  no error. Add the mapping and a test in the same change.
- **Layout is deterministic and tested.** `tests/test_layout.cpp` will fail on
  incidental geometry changes; that is the point (`docs/sacm/sacm-layout-policy.md`).
- **Layout metadata must never reach strict SACM output.** Enforced by
  `cmake/check_layer_gates.cmake` at configure time.
- **Save comes from the library model**, not the projection. Adding a field the
  projection carries but the library does not means it is lost on save — and
  `tests/test_projection_coverage.cpp` / SACM23-LIB-002 exist because of exactly
  this class of bug.
