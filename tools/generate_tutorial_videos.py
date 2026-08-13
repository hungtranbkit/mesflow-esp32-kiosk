#!/usr/bin/env python3
"""Generate silent tutorials from a fresh ESP firmware framebuffer capture."""

import argparse
import hashlib
import json
import subprocess
import tempfile
import textwrap
from datetime import datetime
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT.parent / "artifacts/esp-kiosk/tutorial"
VIDEO_OUT = OUT / "videos"
SOURCE = ROOT / "esp/mesflow_app.cpp"
TUTORIAL_VERSION_FILE = ROOT / "TUTORIAL_VERSION.txt"
FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"
FONT_BOLD = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"

VIDEO_DEFINITIONS = [
    ("00_kiosk_overview.mp4", "Tổng quan ESP Kiosk", [
        ("ready", "Quét thẻ nhân viên"), ("employee_ok", "Quét mã công đoạn"),
        ("working", "Phiên làm việc đã bắt đầu"), ("confirm_qty", "Kiểm tra rồi xác nhận"),
    ], ["A", "B", "C", "D", "E", "F", "G", "H"]),
    ("01_kiosk_boot_connect.mp4", "Khởi động và kết nối", [
        ("offline", "Đang kết nối lại"), ("maintenance", "Màn hình bảo trì"),
        ("ready", "Kiosk đã sẵn sàng"),
    ], ["A", "K"]),
    ("02_kiosk_start_session.mp4", "Bắt đầu phiên làm việc", [
        ("ready", "Quét thẻ nhân viên"), ("employee_ok", "Đã nhận nhân viên"),
        ("operation_ok", "Kiểm tra công đoạn • # Bắt đầu"),
        ("working", "Đã bắt đầu • tự về màn sẵn sàng"),
    ], ["B", "C"]),
    ("03_kiosk_finish_good_qty.mp4", "Kết thúc: chỉ có sản phẩm đạt", [
        ("input_good", "Nhập sản phẩm đạt • # Tiếp"),
        ("input_defect", "Nhập sản phẩm lỗi: 0"),
        ("confirm_qty", "Không hỏi lỗi sửa được khi lỗi = 0"),
        ("finish_success", "Đã ghi nhận"),
    ], ["D", "G", "H"]),
    ("04_kiosk_defect_rework.mp4", "Kết thúc: có lỗi", [
        ("input_good", "Nhập sản phẩm đạt"), ("input_defect", "Nhập tổng sản phẩm lỗi"),
        ("ask_rework", "1 Không • 2 Có lỗi sửa được"),
        ("input_rework", "Nhập số lỗi sửa được"),
        ("confirm_qty", "# Xác nhận • * Quay lại"),
    ], ["E", "F", "G"]),
    ("05_kiosk_common_errors.mp4", "Các lỗi thường gặp", [
        ("error_state", "Quét công đoạn trước thẻ: bị từ chối"),
        ("error_short", "QR sai: không tạo phiên"),
        ("input_rework", "Lỗi sửa được không lớn hơn lỗi tổng"),
        ("input_good", "Không thao tác 2 phút: về màn sẵn sàng"),
    ], ["H", "I", "J"]),
    ("06_kiosk_offline_reconnect.mp4", "Mất mạng và kết nối lại", [
        ("offline", "Mất mạng • kiosk đang thử lại"),
        ("offline_saved", "Giao dịch đã lưu tạm"),
        ("storage_warning", "Bộ nhớ gần đầy • báo quản lý"),
        ("maintenance", "Phím 2: đồng bộ dữ liệu chờ"),
        ("ready", "Kết nối lại • sẵn sàng"),
    ], ["K"]),
]

DESCRIPTIONS = {
    "00_kiosk_overview.mp4": "Tổng quan luồng công nhân sử dụng ESP Kiosk.",
    "01_kiosk_boot_connect.mp4": "Khởi động, kết nối Wi-Fi, máy chủ và trạng thái sẵn sàng.",
    "02_kiosk_start_session.mp4": "Quét thẻ nhân viên và công đoạn để bắt đầu phiên làm việc.",
    "03_kiosk_finish_good_qty.mp4": "Kết thúc phiên khi chỉ có sản phẩm đạt.",
    "04_kiosk_defect_rework.mp4": "Nhập sản phẩm lỗi và phần lỗi có thể sửa được.",
    "05_kiosk_common_errors.mp4": "Nhận biết và xử lý các lỗi thao tác thường gặp.",
    "06_kiosk_offline_reconnect.mp4": "Mất mạng, lưu tạm và đồng bộ lại khi kết nối phục hồi.",
}

STATE_KEYS = {
    "ready": "Không cần bấm phím",
    "employee_ok": "*  Hủy",
    "operation_ok": "*  Quay lại     #  Bắt đầu",
    "working": "Màn giữ 10 giây rồi tự trở về",
    "input_good": "*  Xóa     #  Tiếp",
    "input_defect": "*  Xóa     #  Tiếp",
    "ask_rework": "1  Không     2  Có     *  Quay lại",
    "input_rework": "*  Xóa     #  Tiếp",
    "confirm_qty": "*  Quay lại     #  Xác nhận",
    "finish_success": "Tự trở về màn sẵn sàng",
    "error_state": "*  Quay lại",
    "error_short": "*  Quay lại",
    "offline": "Tự động thử kết nối lại",
    "offline_saved": "Tự đồng bộ khi mạng trở lại",
    "storage_warning": "*  Quay lại",
    "maintenance": "1  Wi-Fi     2  Đồng bộ     #  Thoát",
}


def run(*args):
    subprocess.run(args, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def git_commit():
    try:
        return subprocess.check_output(["git", "-C", str(ROOT), "rev-parse", "HEAD"], text=True).strip()
    except subprocess.CalledProcessError:
        return None


def source_version():
    for line in SOURCE.read_text(encoding="utf-8").splitlines():
        if "APP_VERSION" in line and '"' in line:
            return line.split('"')[1]
    raise RuntimeError("APP_VERSION not found")


def generator_fingerprint():
    digest=hashlib.sha256()
    for path in (Path(__file__),ROOT/"scripts/build-tutorial-package.sh",ROOT/"scripts/package-tutorial.sh"):
        digest.update(path.read_bytes())
    return digest.hexdigest()


def capture_index(capture):
    result = {}
    for metadata in capture.rglob("*.json"):
        item = json.loads(metadata.read_text(encoding="utf-8"))
        screen = item.get("screen")
        png = metadata.with_suffix(".png")
        if screen and png.exists():
            result[screen] = (png, item)
    return result


def render_frame(captured, screen, overlay, firmware, target):
    path, metadata = captured[screen]
    if metadata.get("version") != firmware:
        raise RuntimeError(f"capture/source version mismatch for {screen}")
    canvas = Image.new("RGB", (1280, 720), "#07111f")
    draw = ImageDraw.Draw(canvas)
    lcd = Image.open(path).convert("RGB")
    if lcd.size != (240, 320):
        raise RuntimeError(f"unexpected framebuffer size {lcd.size}: {path}")
    lcd = lcd.resize((420, 560), Image.Resampling.NEAREST)
    canvas.paste(lcd, (64, 70))
    draw.rounded_rectangle((48, 54, 500, 646), 18, outline="#2d79c7", width=3)
    y = 180
    for line in textwrap.wrap(overlay, width=30):
        draw.text((548, y), line, font=ImageFont.truetype(FONT_BOLD, 34), fill="#ffffff")
        y += 48
    y += 34
    draw.text((548, y), f"Màn firmware: {metadata.get('state', screen)}", font=ImageFont.truetype(FONT, 24), fill="#67b7ff")
    draw.text((548, y + 58), STATE_KEYS[screen], font=ImageFont.truetype(FONT, 25), fill="#d9e7f5")
    draw.text((548, 570), "Framebuffer mới capture từ ESP đang chạy", font=ImageFont.truetype(FONT, 20), fill="#8299b2")
    draw.text((48, 680), firmware, font=ImageFont.truetype(FONT, 18), fill="#748ba3")
    canvas.save(target)


def build_video(captured, definition, firmware, work):
    filename, title, slides, cases = definition
    parts = []
    for index, (screen, overlay) in enumerate(slides):
        png = work / f"{filename}-{index}.png"
        part = work / f"{filename}-{index}.mp4"
        render_frame(captured, screen, overlay, firmware, png)
        hold = 5 if screen in {"maintenance", "confirm_qty", "ask_rework"} else 4
        run("ffmpeg", "-y", "-loop", "1", "-i", str(png), "-t", str(hold),
            "-vf", "format=yuv420p", "-an", "-c:v", "libx264", "-preset", "veryfast", "-r", "25", str(part))
        parts.append(part)
    listing = work / f"{filename}.txt"
    listing.write_text("".join(f"file '{part}'\n" for part in parts), encoding="utf-8")
    run("ffmpeg", "-y", "-f", "concat", "-safe", "0", "-i", str(listing), "-map", "0:v:0", "-c", "copy", "-an", str(VIDEO_OUT / filename))
    duration=sum(5 if screen in {"maintenance", "confirm_qty", "ask_rework"} else 4 for screen, _ in slides)
    return {"id": filename.removesuffix(".mp4"), "filename": filename,
            "title": title, "description": DESCRIPTIONS[filename],
            "order": int(filename[:2]), "cases": cases, "duration_seconds": duration}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--capture-dir", required=True, type=Path,
                        help="Fresh output directory produced by capture_esp_ui.py")
    parser.add_argument("--source-fingerprint", required=True,
                        help="SHA256 calculated by the local build orchestrator")
    args = parser.parse_args()
    capture = args.capture_dir.resolve()
    captured = capture_index(capture)
    firmware = source_version()
    tutorial_version=TUTORIAL_VERSION_FILE.read_text(encoding="utf-8").strip()
    required = {screen for _, _, slides, _ in VIDEO_DEFINITIONS for screen, _ in slides}
    missing = sorted(required - captured.keys())
    if missing:
        raise SystemExit("fresh capture is missing screens: " + ", ".join(missing))
    VIDEO_OUT.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="mesflow-kiosk-silent-video-") as temp:
        videos = [build_video(captured, item, firmware, Path(temp)) for item in VIDEO_DEFINITIONS]
    manifest = {
        "type": "esp-kiosk-tutorial",
        "tutorial_version": tutorial_version,
        "firmware_version": firmware,
        "generated_at": datetime.now().astimezone().isoformat(timespec="seconds"),
        "source_commit": git_commit(),
        "source_fingerprint": args.source_fingerprint,
        "generator_fingerprint": generator_fingerprint(),
        "render_method": "firmware-framebuffer-simulator",
        "capture_method": "fresh-device-framebuffer-from-current-firmware-debug-renderer",
        "capture_directory": str(capture),
        "audio": False,
        "cases_covered": sorted({case for item in videos for case in item["cases"]}),
        "videos": videos,
    }
    (OUT / "manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"Regenerated {len(videos)} silent videos from {capture}")


if __name__ == "__main__":
    main()
