#!/usr/bin/env python3
"""Generate compact monochrome MESFlow UTF-8 font subsets for ESP32."""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

FONT = r"C:\Windows\Fonts\arialbd.ttf"
OUT = Path(__file__).resolve().parents[1] / "esp" / "mesflow_vietnamese_font.h"
VI = "ăâêôơưđĂÂÊÔƠƯĐáàảãạấầẩẫậắằẳẵặéèẻẽẹếềểễệíìỉĩịóòỏõọốồổỗộớờởỡợúùủũụứừửữựýỳỷỹỵÁÀẢÃẠẤẦẨẪẬẮẰẲẴẶÉÈẺẼẸẾỀỂỄỆÍÌỈĨỊÓÒỎÕỌỐỒỔỖỘỚỜỞỠỢÚÙỦŨỤỨỪỬỮỰÝỲỶỸỴ"
CHARS = sorted(set(chr(i) for i in range(32, 127)) | set(VI), key=ord)


def emit(size):
    font = ImageFont.truetype(FONT, size)
    ascent, descent = font.getmetrics()
    bitmap, glyphs = [], []
    for ch in CHARS:
        cp = ord(ch)
        bbox = font.getbbox(ch, anchor="ls")
        x0, y0, x1, y1 = bbox
        width, height = max(0, x1 - x0), max(0, y1 - y0)
        offset = len(bitmap)
        if width and height:
            image = Image.new("1", (width, height), 0)
            ImageDraw.Draw(image).text((-x0, -y0), ch, font=font, fill=1, anchor="ls")
            bits = list(image.getdata())
            for start in range(0, len(bits), 8):
                value = 0
                for bit in bits[start:start + 8]:
                    value = (value << 1) | bit
                value <<= max(0, 8 - len(bits[start:start + 8]))
                bitmap.append(value)
        advance = max(1, round(font.getlength(ch)))
        glyphs.append((cp, offset, width, height, advance, x0, y0))
    return ascent, ascent + descent + 2, bitmap, glyphs


lines = ["#pragma once", "#include <Arduino.h>", "",
         "struct MesflowGlyph { uint32_t codepoint; uint32_t bitmapOffset; uint8_t width; uint8_t height; uint8_t advance; int8_t xOffset; int8_t yOffset; };",
         "struct MesflowFont { const uint8_t* bitmap; const MesflowGlyph* glyphs; uint16_t glyphCount; uint8_t ascent; uint8_t lineHeight; };", ""]
for size in (12, 16, 24):
    ascent, line_height, bitmap, glyphs = emit(size)
    lines.append(f"static const uint8_t MF_BITMAP_{size}[] PROGMEM = {{")
    for i in range(0, len(bitmap), 20):
        lines.append("  " + ",".join(f"0x{x:02X}" for x in bitmap[i:i + 20]) + ",")
    lines.append("};")
    lines.append(f"static const MesflowGlyph MF_GLYPHS_{size}[] PROGMEM = {{")
    for g in glyphs:
        lines.append("  {%d,%d,%d,%d,%d,%d,%d}," % g)
    lines += ["};", f"static const MesflowFont MF_FONT_{size} = {{MF_BITMAP_{size}, MF_GLYPHS_{size}, {len(glyphs)}, {ascent}, {line_height}}};", ""]

OUT.write_text("\n".join(lines), encoding="utf-8")
print(f"Generated {OUT} ({OUT.stat().st_size} bytes source, {len(CHARS)} glyphs/font)")
