#!/usr/bin/env python3
"""Render the Windows installer's wizard images from the application icon.

Writes packaging/windows/art/:

    wizard-<dpi>.png        the Welcome and Finish pages' side panel
    wizard-small-<dpi>.png  the top-right image on every other page

at each DPI setting's exact image-area size for the modern wizard style of
Inno Setup 6.6 and later, so Inno Setup never has to stretch one.
The side panel is the application's dark background with the shield icon and a
small goal structure beneath it -- a Goal supported by a Strategy over two
Solutions, drawn in GSN's shapes. Colours are the dark theme's (src/ui/theme.cpp).

The images are committed; rerun this after changing the icon or the palette:

    python tools/release/render_installer_art.py

Requires Pillow.
"""

from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter

REPO = Path(__file__).resolve().parents[2]
ICON = REPO / "assets" / "app_settings" / "icon.png"
OUT = REPO / "packaging" / "windows" / "art"

# Image-area sizes per DPI setting, from the WizardImageFile and
# WizardSmallImageFile help topics (modern style, Inno Setup 6.6+). Not quite
# proportional to DPI: Inno Setup scales by the font's size, not the DPI.
PANEL_SIZES = {100: (202, 386), 125: (269, 515), 150: (336, 643), 175: (403, 772),
               200: (430, 824), 225: (498, 953), 250: (534, 1022)}
SMALL_SIZES = {100: 58, 125: 77, 150: 97, 175: 116, 200: 124, 225: 143, 250: 159}
# The artwork is laid out on a 164x314 grid (the panel's 164:314 aspect ratio,
# which every size above keeps) and a 55x55 one.
PANEL_SIZE = (164, 314)
SMALL_SIZE = (55, 55)
# Drawn once at this multiple of 100 % and scaled down, so every size is
# antialiased the same way.
SUPERSAMPLE = 5

BACKGROUND_TOP = (0x0B, 0x0F, 0x14)
BACKGROUND_BOTTOM = (0x12, 0x1D, 0x33)
ACCENT = (0x4C, 0x8D, 0xFF)


def _gradient(size, top, bottom):
    width, height = size
    image = Image.new("RGB", size)
    draw = ImageDraw.Draw(image)
    for y in range(height):
        t = y / max(height - 1, 1)
        colour = tuple(round(a + (b - a) * t) for a, b in zip(top, bottom))
        draw.line([(0, y), (width, y)], fill=colour)
    return image


def _shield(height):
    icon = Image.open(ICON).convert("RGBA")
    icon = icon.crop(icon.getbbox())
    width = round(icon.width * height / icon.height)
    return icon.resize((width, height), Image.LANCZOS)


def _arrow(draw, start, end, unit, colour):
    """A GSN SupportedBy link: a solid line ending in a solid arrowhead."""
    (x0, y0), (x1, y1) = start, end
    length = ((x1 - x0) ** 2 + (y1 - y0) ** 2) ** 0.5
    dx, dy = (x1 - x0) / length, (y1 - y0) / length
    head = 5 * unit
    base = (x1 - dx * head, y1 - dy * head)
    draw.line([start, base], fill=colour, width=round(1.2 * unit))
    normal = (-dy * head * 0.55, dx * head * 0.55)
    draw.polygon([end, (base[0] + normal[0], base[1] + normal[1]), (base[0] - normal[0], base[1] - normal[1])],
                 fill=colour)


def _text_lines(draw, left, top, widths, unit, colour):
    """Placeholder statement text: short rounded bars, one per line."""
    for index, width in enumerate(widths):
        y = top + index * 4.2 * unit
        draw.rounded_rectangle([left, y, left + width * unit, y + 1.8 * unit], radius=0.9 * unit, fill=colour)


def render_panel():
    unit = SUPERSAMPLE
    size = (PANEL_SIZE[0] * unit, PANEL_SIZE[1] * unit)
    panel = _gradient(size, BACKGROUND_TOP, BACKGROUND_BOTTOM).convert("RGBA")

    # A soft accent glow behind the shield.
    glow = Image.new("RGBA", size, (0, 0, 0, 0))
    ImageDraw.Draw(glow).ellipse([22 * unit, 22 * unit, 142 * unit, 142 * unit], fill=ACCENT + (70,))
    panel = Image.alpha_composite(panel, glow.filter(ImageFilter.GaussianBlur(24 * unit)))

    shield = _shield(92 * unit)
    panel.alpha_composite(shield, ((size[0] - shield.width) // 2, 36 * unit))

    # The goal structure, drawn on its own layer so it can sit back at partial
    # opacity without its overlapping strokes darkening where they meet.
    layer = Image.new("RGBA", size, (0, 0, 0, 0))
    draw = ImageDraw.Draw(layer)
    stroke = ACCENT + (255,)
    text = (0xE8, 0xED, 0xF5, 150)
    line = round(1.2 * unit)

    # Goal: a rectangle.
    goal = [34 * unit, 170 * unit, 130 * unit, 198 * unit]
    draw.rectangle(goal, outline=stroke, width=line)
    _text_lines(draw, 42 * unit, 178 * unit, (70, 48), unit, text)

    # Strategy: a parallelogram.
    top, bottom = 216 * unit, 238 * unit
    draw.polygon([(46 * unit, top), (128 * unit, top), (118 * unit, bottom), (36 * unit, bottom)],
                 outline=stroke, width=line)
    _text_lines(draw, 52 * unit, 223 * unit, (56,), unit, text)

    # Solutions: circles.
    radius = 14 * unit
    solutions = [(56 * unit, 275 * unit), (108 * unit, 275 * unit)]
    for cx, cy in solutions:
        draw.ellipse([cx - radius, cy - radius, cx + radius, cy + radius], outline=stroke, width=line)
        _text_lines(draw, cx - 7 * unit, cy - 1 * unit, (14,), unit, text)

    _arrow(draw, (82 * unit, 198 * unit), (82 * unit, top), unit, stroke)
    for cx, cy in solutions:
        _arrow(draw, (82 * unit, bottom), (cx, cy - radius), unit, stroke)

    faded = layer.copy()
    faded.putalpha(layer.getchannel("A").point(lambda alpha: alpha * 150 // 255))
    return Image.alpha_composite(panel, faded).convert("RGB")


def render_small():
    unit = SUPERSAMPLE
    size = (SMALL_SIZE[0] * unit, SMALL_SIZE[1] * unit)
    image = Image.new("RGBA", size, (0, 0, 0, 0))
    shield = _shield(50 * unit)
    image.alpha_composite(shield, ((size[0] - shield.width) // 2, (size[1] - shield.height) // 2))
    return image


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    panel, small = render_panel(), render_small()
    for old in OUT.glob("wizard*.png"):
        old.unlink()
    for dpi, size in PANEL_SIZES.items():
        panel.resize(size, Image.LANCZOS).save(OUT / f"wizard-{dpi}.png", optimize=True)
    for dpi, side in SMALL_SIZES.items():
        small.resize((side, side), Image.LANCZOS).save(OUT / f"wizard-small-{dpi}.png", optimize=True)
    print(f"wrote {len(PANEL_SIZES) + len(SMALL_SIZES)} images to {OUT.relative_to(REPO)}")


if __name__ == "__main__":
    main()
