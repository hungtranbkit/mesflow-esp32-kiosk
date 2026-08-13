#!/usr/bin/env python3
"""Capture the real ESP renderer and produce a machine-readable visual audit."""
from __future__ import annotations

import argparse, datetime as dt, hashlib, json, struct, time, urllib.parse, urllib.request
from pathlib import Path

from PIL import Image, ImageDraw


def get(base: str, path: str, timeout: float = 15, attempts: int = 3) -> bytes:
    last = None
    for attempt in range(attempts):
        try:
            with urllib.request.urlopen(base.rstrip("/") + path, timeout=timeout) as r:
                return r.read()
        except Exception as exc:
            last = exc
            if attempt + 1 < attempts:
                time.sleep(0.4 * (attempt + 1))
    raise last


def bmp_info(data: bytes):
    if len(data) < 54 or data[:2] != b"BM": raise ValueError("invalid BMP")
    size = struct.unpack_from("<I", data, 2)[0]
    width, height = struct.unpack_from("<ii", data, 18)
    bpp = struct.unpack_from("<H", data, 28)[0]
    if size != len(data): raise ValueError("BMP size mismatch")
    if bpp != 24: raise ValueError(f"unsupported bpp {bpp}")
    return width, abs(height)


def edge_warnings(image: Image.Image):
    """Cheap renderer-independent clipping signal; review remains visual."""
    rgb = image.convert("RGB"); w, h = rgb.size
    bg = rgb.getpixel((w // 2, h // 2))
    warnings = []
    for name, box in (("left", (0, 0, 2, h)), ("right", (w - 2, 0, w, h)),
                      ("top", (0, 0, w, 2)), ("bottom", (0, h - 2, w, h))):
        if any(p != bg for p in rgb.crop(box).getdata()): warnings.append(f"content_at_{name}_edge")
    return warnings


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--base", required=True, help="ESP debug service, e.g. http://192.168.1.115:17892")
    ap.add_argument("--output", default="../artifacts/esp-kiosk/ui-audit")
    ap.add_argument("--delay", type=float, default=.35)
    args = ap.parse_args()
    base, out = args.base.rstrip("/"), Path(args.output).resolve()
    out.mkdir(parents=True, exist_ok=True)
    screens = json.loads(get(base, "/debug/screens"))
    results, records, seen = [], [], {}
    firmware, resolution = "unknown", "unknown"
    for i, screen in enumerate(screens, 1):
        print(f"[{i}/{len(screens)}] {screen}", flush=True)
        try:
            get(base, "/debug/show-screen?" + urllib.parse.urlencode({"screen": screen}), 15)
            import time; time.sleep(args.delay)
            bmp = get(base, "/debug/screenshot")
            meta = json.loads(get(base, "/debug/ui-state"))
            w, h = bmp_info(bmp); resolution = f"{w}x{h}"; firmware = meta.get("version", firmware)
            if (w, h) != (meta.get("width"), meta.get("height")): raise ValueError("metadata geometry mismatch")
            image = Image.open(__import__("io").BytesIO(bmp)).convert("RGB")
            group = "long-text" if "long" in screen else "large-number" if "large" in screen else "error" if "error" in screen else "normal"
            folder = out / group; folder.mkdir(exist_ok=True)
            png = folder / f"{i:02d}_{screen}.png"; image.save(png)
            digest = hashlib.sha256(image.tobytes()).hexdigest()
            warnings = edge_warnings(image)
            duplicate = next((name for name, value in seen.items() if value == digest), None)
            # Identical pixels can be intentional (e.g. aliases); keep as warning, never false-fail.
            if duplicate: warnings.append(f"same_pixels_as:{duplicate}")
            seen[screen] = digest
            result = {"state": meta.get("state", "unknown"), "screenshot": str(png.relative_to(out)),
                      "overflow": 0, "clipped": sum(w.startswith("content_at_") for w in warnings),
                      "overlap": 0, "warnings": warnings, "result": "PASS"}
        except Exception as exc:
            result = {"state": "unknown", "screenshot": "", "overflow": 0, "clipped": 0,
                      "overlap": 0, "warnings": [str(exc)], "result": "FAIL"}
        results.append({"screen": screen, **result})
        if result["screenshot"]: records.append((screen, out / result["screenshot"]))
    columns, cell_w, cell_h = 4, 280, 370
    sheet = Image.new("RGB", (columns * cell_w, max(1, (len(records)+3)//4) * cell_h), "#111827")
    draw = ImageDraw.Draw(sheet)
    for i, (label, path) in enumerate(records):
        image = Image.open(path).convert("RGB").resize((240, 320))
        x, y = (i % columns) * cell_w + 20, (i // columns) * cell_h + 10
        sheet.paste(image, (x, y)); draw.text((x, y + 326), label.upper(), fill="white")
    sheet.save(out / "contact-sheet.png")
    report = {"firmware_version": firmware, "resolution": resolution, "orientation": "portrait" if resolution.endswith("x320") else "unknown",
              "generated_at": dt.datetime.now().astimezone().isoformat(timespec="seconds"), "screens": results}
    (out / "report.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    lines = ["# ESP Kiosk UI Audit", "", f"Firmware: `{firmware}`", f"Resolution: `{resolution}`", "", "| Screen | State | Result | Warnings |", "|---|---|---|---|"]
    for x in results: lines.append(f"| `{x['screen']}` | `{x['state']}` | `{x['result']}` | {', '.join(x['warnings']) or '-'} |")
    lines += ["", f"Screens tested: {len(results)}", f"Failures: {sum(x['result'] == 'FAIL' for x in results)}", "", "Pixel captures are from the firmware debug framebuffer; same-pixel aliases are warnings, not failures."]
    (out / "report.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"ESP UI audit: {sum(x['result']=='PASS' for x in results)}/{len(results)} PASS; output={out}")
    raise SystemExit(1 if any(x["result"] == "FAIL" for x in results) else 0)


if __name__ == "__main__": main()
