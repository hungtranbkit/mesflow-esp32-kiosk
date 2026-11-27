"""Check actual bitmap glyph pixels against the LCD header geometry."""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "esp/mesflow_app.cpp").read_text()
FONT = (ROOT / "esp/mesflow_vietnamese_font.h").read_text()


def font_data(size):
    bitmap = re.search(
        rf"MF_BITMAP_{size}\[\].*?=\s*\{{(.*?)\}};", FONT, re.S).group(1)
    bitmap = bytes(int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]+)", bitmap))
    table = re.search(
        rf"MF_GLYPHS_{size}\[\].*?=\s*\{{(.*?)\}};", FONT, re.S).group(1)
    glyphs = {}
    for row in re.findall(r"\{([^}]+)\}", table):
        cp, offset, width, height, advance, xoff, yoff = map(int, row.split(","))
        glyphs[cp] = (offset, width, height, advance, xoff, yoff)
    metrics = re.search(
        rf"MF_FONT_{size}\s*=\s*\{{MF_BITMAP_{size},\s*MF_GLYPHS_{size},\s*\d+,\s*(\d+),\s*(\d+)\s*\}};",
        FONT)
    return bitmap, glyphs, tuple(map(int, metrics.groups()))


def pixels(bitmap, glyph, x, baseline):
    offset, width, height, _, xoff, yoff = glyph
    for yy in range(height):
        for xx in range(width):
            bit = yy * width + xx
            if bitmap[offset + bit // 8] & (0x80 >> (bit % 8)):
                yield x + xoff + xx, baseline + yoff + yy


def test_vietnamese_name_glyphs_fit_above_id_without_top_clipping():
    # Real drawTextBox call and font metrics. Its single line is vertically
    # centered; every nonzero bitmap pixel must fit on screen above the ID.
    x, y, height = map(int, re.search(
        r"drawTextBox\(DEVICE_NAME,\s*(\d+),\s*(\d+),\s*CLOCK_X - \d+,\s*(\d+),",
        SOURCE).groups())
    id_y = int(re.search(r"printText\(10,\s*(\d+),\s*label,", SOURCE).group(1))
    id_bitmap, id_glyphs, (id_ascent, _) = font_data(12)
    id_top = min(yy for cp in "ID 02"
                 for _, yy in pixels(id_bitmap, id_glyphs[ord(cp)], 10, id_y + id_ascent))
    for size in (16, 12):  # preferred name font and existing long-name fallback
        bitmap, glyphs, (ascent, line_height) = font_data(size)
        baseline = y + max(0, (height - line_height) // 2) + ascent
        for cp, glyph in glyphs.items():
            ink = list(pixels(bitmap, glyph, x, baseline))
            if not ink:
                continue
            assert min(yy for _, yy in ink) >= 0, f"clipped top: U+{cp:04X}, font {size}"
            assert max(yy for _, yy in ink) < id_top, f"name/ID collision: U+{cp:04X}"


def test_display_numbers_fit_in_header_without_clock_overlap():
    bitmap, glyphs, (ascent, _) = font_data(12)
    id_y = int(re.search(r"printText\(10,\s*(\d+),\s*label,", SOURCE).group(1))
    clock_x = int(re.search(r"CLOCK_X = (\d+);", SOURCE).group(1))
    header_h = int(re.search(r"HEADER_H = (\d+);", SOURCE).group(1))
    for text in ("ID --", "ID 01", "ID 02", "ID 100", "ID 2147483647"):
        x, ink = 10, []
        for cp in text:
            glyph = glyphs[ord(cp)]
            ink.extend(pixels(bitmap, glyph, x, id_y + ascent))
            x += glyph[3]
        assert min(xx for xx, _ in ink) >= 0
        assert max(xx for xx, _ in ink) < clock_x
        assert max(yy for _, yy in ink) < header_h - 1  # retain separator
