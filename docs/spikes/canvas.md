# Canvas spike: findings

**Authority: evidence.** A record of what a throwaway experiment measured on
2026-11-15. The spike code is not in this repository.

## The question

The plan for the Rust rebuild has layout run in Rust, with the frontend only
drawing, so that the canvas and the SVG export share one geometry. That only
works if Rust can measure text the way a browser draws it, including Japanese,
without asking the browser. The spike tested that, and how the result should be
drawn.

## What was built

- A Rust crate that measures and wraps text, lays out a diagram, and writes SVG
  from the same geometry. It was compiled natively and to WebAssembly.
- A web page that draws that geometry two ways, on a Canvas 2D surface and as
  SVG elements, with pan, zoom and editing of a node's text.
- A script that ran the page in Chrome, Edge and Firefox and collected the
  numbers below.

Text was shaped with `rustybuzz`, a Rust port of HarfBuzz, and broken into lines
with the Unicode line-breaking algorithm (`unicode-linebreak`). The font was
Noto Sans JP Regular at 14 px, the font the C++ application ships.

The data was the bilingual Open Autonomy sample case (87 elements, each with
English and Japanese text) and generated trees of about 1,500 and 7,500 nodes.

## Findings

### 1. Rust measures text as the browser draws it

Each wrapped line was measured in Rust and again by the browser
(`measureText`). Error is the absolute difference in pixels.

| Browser | Text | Lines | Mean error | Largest error |
|---|---|---:|---:|---:|
| Chrome 157, Edge 157 | English | 175 | 0.000 | 0.000 |
| Chrome 157, Edge 157 | Japanese | 177 | 0.013 | 1.400 |
| Firefox 159 | English | 175 | 0.007 | 0.053 |
| Firefox 159 | Japanese | 177 | 0.017 | 1.400 |

- **Shaping is needed.** Summing each character's advance without shaping was
  wrong by up to 2.2 px on an English line, because browsers apply kerning.
- **The one Japanese difference has a known cause.** The font defines kerning
  between some katakana pairs. HarfBuzz applies it; Chrome and Firefox do not.
  Turning kerning off for that line gives the browser's width exactly. The
  largest effect seen was 1.4 px on a 210 px line.

### 2. Draw on a canvas, not as SVG elements

Frame interval while panning, on a 120 Hz display (8.3 ms is a perfect frame):

| Nodes | Renderer | First paint | Frame interval, mean | Worst frame |
|---:|---|---:|---:|---:|
| 87 | Canvas 2D | 2 to 12 ms | 8.3 to 9.0 ms | 17 ms |
| 87 | SVG elements | 16 to 42 ms | 8.3 to 11 ms | 109 ms |
| 1,496 | Canvas 2D | 6 to 10 ms | 8.3 ms | 10 ms |
| 1,496 | SVG elements | 133 to 213 ms | 8.8 to 17 ms | 200 ms |
| 7,496 | Canvas 2D | 10 to 19 ms | 8.3 ms | 17 ms |
| 7,496 | SVG elements | 0.7 to 1.5 s | 20 to 138 ms | 701 ms |

Ranges cover two sets of runs in Chrome (headless and visible), Edge and
Firefox.

At the size of today's real cases both are smooth. The canvas stays at full
frame rate at every size, because it draws only what is in view. SVG elements
stutter at 1,496 nodes and drop to between 7 and 50 frames per second at 7,496.

These numbers come from a scripted pan at a fixed zoom, which is the easy case
for SVG. Tried by hand at 1,496 nodes, with zooming, the canvas was smooth and
the SVG renderer was noticeably slow: changing the zoom makes the browser draw
every text element again.

### 3. Layout is fast enough, if measurements are kept

| Nodes | First layout, native | First layout, WebAssembly | After editing one node, WebAssembly |
|---:|---:|---:|---:|
| 87 | 1.3 ms | 2.5 to 7 ms | 0.4 to 1.2 ms |
| 1,496 | 114 ms | 160 to 392 ms | 6 to 13 ms |
| 7,496 | 607 ms | 0.8 to 1.5 s | 32 to 93 ms |

- Almost all of the first layout is text shaping. Keeping each node's measured
  size and re-measuring only the node whose text changed makes an edit cheap.
- The edit times include turning the whole graph and the whole geometry into
  JSON and back. At 7,496 nodes that is most of the cost.
- The spike's line breaking re-shapes the growing line at every break
  opportunity. It was not optimised.

### 4. An exported SVG depends on the viewer's font

The SVG written from the geometry looks the same as the canvas when the font is
available. Opened on its own, the file names `Noto Sans JP` and relies on the
viewer having it. With another font the text no longer matches the shapes drawn
for it.

### 5. The WebAssembly package is mostly font

The package was 5.6 MB (4.3 MB compressed), of which 4.5 MB is the embedded
font. The page loads the same font a second time to draw with it.

### 6. None of the sample cases load in libSACM yet

Found on the way, and it affects the first milestone:

- Three sample files use an older SACM 2.2 namespace, which libSACM refuses.
- The kitchen-blender example is refused at its terminology package, which
  libSACM does not read yet.

The spike read the sample with a small throwaway reader instead.

## What this decides

| Decision | Outcome |
|---|---|
| Layout and text measurement in Rust | Confirmed. Shape with `rustybuzz`; break lines with `unicode-linebreak`. |
| Renderer for the diagram | Canvas 2D, drawing only what is in view. Hit testing uses the same geometry. |
| Measurement cache | Required: keep each node's measured size between layouts. |
| Kerning | Apply it to Latin text and not to kana, to match browsers. |

## What it leaves open

- **WebKit was not tested.** Tauri uses WebKit on macOS and Linux. The text
  comparison has to be repeated there before milestone 1 is called done.
- **A canvas is invisible to assistive technology.** The tree view has to carry
  the accessible structure of the argument.
- **SVG export:** embed a subset of the font, or write text as outlines. Outlines
  are exact everywhere but the text can no longer be selected or searched.
- **Font delivery:** load the font once and hand it to the layout code, instead
  of shipping it inside the WebAssembly package and again to the page.
- **Geometry transfer:** send only what changed, or a binary form, once cases
  reach thousands of nodes. JSON is fine below that.
- **Opening real cases:** what the first milestone does with SACM 2.2 namespace
  files, and reading of terminology and artifact packages in libSACM.
- **The layout itself.** The spike used a plain layered tree. It did not attempt
  the GSN layout of the C++ application, and it only edited text.

## Limits of the measurements

- One machine, Windows, a 120 Hz display. Headless and visible Chrome agreed on
  text and on canvas frame times.
- The generated trees are much wider than a real argument, so at most a quarter
  of their nodes were in view at once.
- "Editing" was changing a node's text and laying out again. Dragging,
  connecting and selecting were not tried.
