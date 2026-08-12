#!/usr/bin/env python3
"""Capture and validate every ESP32 kiosk debug UI screen."""

import argparse
import datetime as dt
import json
import struct
import time
import urllib.parse
import urllib.request
from pathlib import Path

try:
    from PIL import Image, ImageDraw
except ImportError:
    Image = ImageDraw = None


def get(url, timeout=45):
    with urllib.request.urlopen(url, timeout=timeout) as response:
        return response.read(), response.headers.get_content_type()


def validate_bmp(data, expected_width, expected_height):
    if len(data) < 54 or data[:2] != b"BM":
        raise ValueError("invalid BMP header")
    size = struct.unpack_from("<I", data, 2)[0]
    width, height = struct.unpack_from("<ii", data, 18)
    bpp = struct.unpack_from("<H", data, 28)[0]
    if size != len(data):
        raise ValueError(f"BMP size mismatch: header={size}, actual={len(data)}")
    if (width, abs(height), bpp) != (expected_width, expected_height, 24):
        raise ValueError(f"unexpected BMP geometry: {width}x{height} {bpp}bpp")
    pixels = data[54:]
    sample = {pixels[i:i + 3] for i in range(0, len(pixels), max(3, len(pixels) // 2048 // 3 * 3))}
    if len(sample) < 2:
        raise ValueError("screenshot is a single color")
    return width, abs(height)


def group_for(screen):
    if "long" in screen:
        return "long-text"
    if "large" in screen:
        return "large-number"
    if "error" in screen:
        return "error"
    return "normal"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", required=True, help="ESP IP or host[:port]")
    parser.add_argument("--port", type=int, default=17892)
    parser.add_argument("--output", default="test-results/esp-ui-capture")
    parser.add_argument("--delay", type=float, default=0.35)
    args = parser.parse_args()
    authority = args.host if ":" in args.host else f"{args.host}:{args.port}"
    base = f"http://{authority}"
    output = Path(args.output).resolve()
    output.mkdir(parents=True, exist_ok=True)

    raw, _ = get(base + "/debug/screens")
    screens = json.loads(raw)
    results, image_records, hashes = [], [], {}
    firmware = "unknown"
    resolution = "unknown"

    for index, screen in enumerate(screens, 1):
        group_dir = output / group_for(screen)
        group_dir.mkdir(parents=True, exist_ok=True)
        stem = f"{index:02d}_{screen}"
        result = {"screen": screen, "state": "unknown", "result": "FAILED", "reason": ""}
        try:
            get(base + "/debug/show-screen?" + urllib.parse.urlencode({"screen": screen}))
            time.sleep(args.delay)
            bmp, content_type = get(base + "/debug/screenshot", timeout=90)
            metadata_raw, _ = get(base + "/debug/ui-state")
            metadata = json.loads(metadata_raw)
            firmware = metadata.get("version", firmware)
            resolution = f"{metadata['width']}x{metadata['height']}"
            validate_bmp(bmp, metadata["width"], metadata["height"])
            digest = hash(bmp[54:])
            current_state = metadata.get("state", "unknown")
            duplicate = next(((name, value[1]) for name, value in hashes.items() if value[0] == digest), None)
            hashes[screen] = (digest, current_state)

            bmp_path = group_dir / f"{stem}.bmp"
            json_path = group_dir / f"{stem}.json"
            bmp_path.write_bytes(bmp)
            json_path.write_text(json.dumps(metadata, ensure_ascii=False, indent=2), encoding="utf-8")
            png_path = None
            if Image:
                png_path = group_dir / f"{stem}.png"
                with Image.open(bmp_path) as image:
                    image.load()
                    image.save(png_path)
            result.update(state=metadata.get("state", "unknown"), result="PASS")
            if duplicate and duplicate[1] != current_state:
                result.update(result="FAILED", reason=f"pixels identical to different state {duplicate[0]}")
            image_records.append((screen, png_path or bmp_path))
        except Exception as exc:
            result["reason"] = str(exc)
        results.append(result)

    contact_path = output / "contact-sheet.png"
    if Image and image_records:
        columns, cell_w, cell_h = 4, 280, 370
        rows = (len(image_records) + columns - 1) // columns
        sheet = Image.new("RGB", (columns * cell_w, rows * cell_h), "#111827")
        draw = ImageDraw.Draw(sheet)
        for i, (label, path) in enumerate(image_records):
            with Image.open(path) as source:
                source.load()
                image = source.convert("RGB")
            x = (i % columns) * cell_w + 20
            y = (i // columns) * cell_h + 10
            sheet.paste(image, (x, y))
            draw.text((x, y + 326), label.upper(), fill="white")
        sheet.save(contact_path)

    captured_at = dt.datetime.now().astimezone().isoformat(timespec="seconds")
    lines = [
        "# ESP32 UI Capture Report", "",
        f"Firmware version: {firmware}  ", f"Resolution: {resolution}  ",
        f"ESP URL: {base}  ", f"Capture time: {captured_at}  ", "", "## Screens", "",
        "| Screen | Screenshot | State | Result |", "|---|---|---|---|",
    ]
    for index, result in enumerate(results, 1):
        rel = f"{group_for(result['screen'])}/{index:02d}_{result['screen']}.png"
        status = result["result"] + ((": " + result["reason"]) if result["reason"] else "")
        lines.append(f"| {result['screen']} | [{index:02d}_{result['screen']}.png]({rel}) | {result['state']} | {status} |")
    passed = sum(item["result"] == "PASS" for item in results)
    lines += ["", "## Checks", "", f"- Screenshot API: {'PASS' if results else 'FAILED'}",
              f"- Resolution: {'PASS' if resolution == '240x320' else 'FAILED'}",
              f"- BMP decode: {'PASS' if passed else 'FAILED'}",
              f"- UI state endpoint: {'PASS' if results else 'FAILED'}",
              f"- Long text variants: {'PASS' if any(r['result'] == 'PASS' and 'long' in r['screen'] for r in results) else 'FAILED'}",
              f"- Large number variants: {'PASS' if any(r['result'] == 'PASS' and 'large' in r['screen'] for r in results) else 'FAILED'}",
              "", f"Captured: {passed}/{len(results)} PASS."]
    (output / "report.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"Captured {passed}/{len(results)} screens into {output}")
    if passed != len(results):
        raise SystemExit(1)


if __name__ == "__main__":
    main()
