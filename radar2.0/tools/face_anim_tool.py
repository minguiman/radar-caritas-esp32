import argparse
import html
import json
import re
import shutil
import struct
from dataclasses import dataclass
from pathlib import Path

from PIL import Image


PROJECT_ROOT = Path(__file__).resolve().parents[1]
WORKSPACE_ROOT = PROJECT_ROOT.parent
DEFAULT_RAW_ROOT = WORKSPACE_ROOT / "caritas_sd_rgb565"
DEFAULT_MANIFEST = DEFAULT_RAW_ROOT / "manifest_sd.json"
DEFAULT_OUTPUT = WORKSPACE_ROOT / "faces_sd_anim"
MAGIC = b"R2ANIM01"
HEADER_BYTES = 28
ENTRY_BYTES = 20
DEFAULT_CANVAS = (400, 200)
DEFAULT_FPS = 10
DEFAULT_CARITAS_ROOT = WORKSPACE_ROOT / "caritas"


CATEGORY_VARIANTS = {
    "normal": [
        "nhin_ben_phai/1",
        "sleepy/2",
        "distracted/1",
        "nhin_ben_trai/1",
        "happy/1",
        "nhin_xuong/1",
        "nheo_mat/1",
        "hat_xi/1",
        "hat_xi/2",
    ],
    "sleep": ["sleepy/2"],
    "focus": ["serious/1"],
    "agua": ["agua/1"],
    "ojos": ["nheo_mat/1"],
    "stop": ["gian_du/1"],
    "break": ["happy/1", "smile/1"],
    "celebrar": [
        "happy/1",
        "smile/1",
        "keep_it_up/1",
        "star/1",
        "anime_love/1",
        "love/2",
        "love/3",
        "mini_love/1",
    ],
    "llorar": ["cry/1", "cry/2", "crying/1", "sad/1", "sung_nuoc/1", "sung_nuoc/2"],
}


CARITAS_CATEGORY_RULES = {
    "normal": {
        "mac_dinh",
        "neutral",
        "smile",
        "happy",
        "distracted",
        "hat_xi",
        "hehe",
        "nheo_mat",
        "nhin_ben_phai",
        "nhin_ben_trai",
        "nhin_xuong",
        "UwU",
    },
    "sleep": {"sleepy"},
    "focus": {"serious", "left", "hand_some"},
    "agua": {"agua", "sung_nuoc"},
    "ojos": {"nheo_mat", "chong_mat"},
    "stop": {"angry", "gian_du", "devil", "devil_eyes", "yelling"},
    "break": {"smile", "happy", "cute", "hehe", "leu_leu", "dasai"},
    "celebrar": {
        "happy",
        "smile",
        "love",
        "mini_love",
        "anime_love",
        "kissou",
        "keep_it_up",
        "star",
        "fire",
        "xi_lua",
        "xi_khoi",
    },
    "llorar": {"cry", "crying", "sad", "scared"},
}

@dataclass
class Frame:
    pixels: bytes
    width: int
    height: int
    offset_x: int
    offset_y: int
    source_name: str


def safe_name(value: str) -> str:
    value = re.sub(r"[^A-Za-z0-9_.-]+", "_", value.strip().replace("/", "_"))
    return value.strip("_") or "anim"


def rgb888_to_rgb565(r: int, g: int, b: int) -> int:
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def image_to_frame(path: Path, canvas: tuple[int, int]) -> Frame:
    image = Image.open(path).convert("RGBA")
    if image.width > canvas[0] or image.height > canvas[1]:
        image.thumbnail(canvas, Image.Resampling.LANCZOS)

    background = Image.new("RGBA", image.size, (0, 0, 0, 255))
    background.alpha_composite(image)
    image = background.convert("RGB")

    bbox = image.getbbox()
    if bbox is None:
        bbox = (0, 0, 1, 1)
    cropped = image.crop(bbox)
    offset_x = ((canvas[0] - image.width) // 2) + bbox[0]
    offset_y = ((canvas[1] - image.height) // 2) + bbox[1]

    data = bytearray(cropped.width * cropped.height * 2)
    out = 0
    for r, g, b in cropped.getdata():
        value = rgb888_to_rgb565(r, g, b)
        data[out] = value & 0xFF
        data[out + 1] = value >> 8
        out += 2
    return Frame(bytes(data), cropped.width, cropped.height, offset_x, offset_y, path.name)


def frame_from_manifest(raw_root: Path, frame: dict) -> Frame:
    source = raw_root / frame["file"]
    width = int(frame["width"])
    height = int(frame["height"])
    expected = width * height * 2
    pixels = source.read_bytes()
    if len(pixels) != expected:
        raise ValueError(f"{source} has {len(pixels)} bytes, expected {expected}")
    return Frame(
        pixels=pixels,
        width=width,
        height=height,
        offset_x=int(frame["offset_x"]),
        offset_y=int(frame["offset_y"]),
        source_name=Path(frame["file"]).name,
    )


def encode_rle_rgb565(pixels: bytes) -> bytes:
    if len(pixels) % 2 != 0:
        raise ValueError("RGB565 data must have an even byte count")
    out = bytearray()
    previous = None
    count = 0
    for i in range(0, len(pixels), 2):
        value = pixels[i] | (pixels[i + 1] << 8)
        if previous is None:
            previous = value
            count = 1
        elif value == previous and count < 65535:
            count += 1
        else:
            out += struct.pack("<HH", count, previous)
            previous = value
            count = 1
    if previous is not None:
        out += struct.pack("<HH", count, previous)
    return bytes(out)


def write_anim(path: Path, frames: list[Frame], canvas: tuple[int, int], fps: int) -> tuple[int, int]:
    path.parent.mkdir(parents=True, exist_ok=True)
    encoded = [encode_rle_rgb565(frame.pixels) for frame in frames]
    table_offset = HEADER_BYTES
    data_offset = HEADER_BYTES + (len(frames) * ENTRY_BYTES)
    entries = []
    cursor = data_offset
    delay_ms = max(1, int(round(1000 / max(1, fps))))
    for frame, payload in zip(frames, encoded):
        entries.append((cursor, len(payload), frame.width, frame.height,
                        frame.offset_x, frame.offset_y, delay_ms, 0))
        cursor += len(payload)

    with path.open("wb") as handle:
        handle.write(struct.pack(
            "<8sHHHHHHII",
            MAGIC,
            HEADER_BYTES,
            0x0001,
            canvas[0],
            canvas[1],
            fps,
            len(frames),
            table_offset,
            0,
        ))
        for entry in entries:
            handle.write(struct.pack("<IIHHHHHH", *entry))
        for payload in encoded:
            handle.write(payload)

    raw_bytes = sum(len(frame.pixels) for frame in frames)
    return raw_bytes, path.stat().st_size


def natural_key(path: Path) -> list[object]:
    return [int(part) if part.isdigit() else part.lower()
            for part in re.split(r"(\d+)", path.name)]


def ordered_image_files(folder: Path) -> list[Path]:
    supported = {".png", ".jpg", ".jpeg", ".bmp", ".webp"}
    files = {path.name: path for path in folder.iterdir()
             if path.is_file() and path.suffix.lower() in supported}
    order_file = folder / "order.txt"
    if order_file.exists():
        ordered = []
        for line in order_file.read_text(encoding="utf-8").splitlines():
            name = line.strip()
            if name and not name.startswith("#") and name in files:
                ordered.append(files.pop(name))
        return ordered + sorted(files.values(), key=natural_key)
    return sorted(files.values(), key=natural_key)


def prepare_output(output: Path, clean: bool) -> Path:
    faces_root = output / "faces"
    if clean:
        if faces_root.exists():
            shutil.rmtree(faces_root)
        preview_root = output / "_preview"
        if preview_root.exists():
            shutil.rmtree(preview_root)
    faces_root.mkdir(parents=True, exist_ok=True)
    return faces_root


def caritas_categories(expression: str) -> list[str]:
    ordered_categories = [
        "normal",
        "sleep",
        "focus",
        "agua",
        "ojos",
        "stop",
        "break",
        "celebrar",
        "llorar",
    ]
    categories = [
        category for category in ordered_categories
        if expression in CARITAS_CATEGORY_RULES.get(category, set())
    ]
    if categories:
        return categories
    return ["especial"]


def iter_caritas_animation_dirs(root: Path) -> list[tuple[str, str, Path]]:
    animations = []
    for expression_dir in sorted((p for p in root.iterdir() if p.is_dir()), key=lambda p: p.name.lower()):
        expression = expression_dir.name
        if ordered_image_files(expression_dir):
            animations.append((expression, safe_name(expression), expression_dir))
        for folder in sorted((p for p in expression_dir.rglob("*") if p.is_dir()), key=lambda p: p.as_posix().lower()):
            if not ordered_image_files(folder):
                continue
            rel_parts = folder.relative_to(expression_dir).parts
            name = safe_name("_".join((expression, *rel_parts)))
            animations.append((expression, name, folder))
    return animations


def load_variants(manifest_path: Path) -> dict[str, dict]:
    data = json.loads(manifest_path.read_text(encoding="utf-8"))
    variants = {}
    for expression in data["expressions"]:
        expr_id = expression["id"]
        for variant in expression["variants"]:
            key = f"{expr_id}/{variant['id']}"
            variants[key] = variant
    return variants


def rgb565_to_png(frame: Frame, canvas: tuple[int, int], path: Path) -> None:
    image = Image.new("RGB", (frame.width, frame.height))
    pixels = []
    for i in range(0, len(frame.pixels), 2):
        value = frame.pixels[i] | (frame.pixels[i + 1] << 8)
        r = ((value >> 11) & 0x1F) << 3
        g = ((value >> 5) & 0x3F) << 2
        b = (value & 0x1F) << 3
        pixels.append((r, g, b))
    image.putdata(pixels)
    preview = Image.new("RGB", canvas, (0, 0, 0))
    preview.paste(image, (frame.offset_x, frame.offset_y))
    preview.save(path)


def write_preview(output_root: Path, rows: list[dict]) -> None:
    preview_dir = output_root / "_preview"
    preview_dir.mkdir(parents=True, exist_ok=True)
    items = []
    for row in rows:
        rel = row["preview"].relative_to(output_root).as_posix()
        items.append(
            f"<article><img src='{html.escape(rel)}'><h2>{html.escape(row['category'])}/"
            f"{html.escape(row['name'])}</h2><p>{row['frames']} frames | "
            f"{row['raw_kb']:.1f} KB raw | {row['anim_kb']:.1f} KB .anim</p></article>"
        )

    (output_root / "_preview.html").write_text(f"""<!doctype html>
<html lang="es">
<meta charset="utf-8">
<title>Face Anim Preview</title>
<style>
body {{ margin: 24px; background: #101214; color: #f4efe5; font-family: Georgia, serif; }}
.grid {{ display: grid; grid-template-columns: repeat(auto-fill, minmax(220px, 1fr)); gap: 16px; }}
article {{ border: 1px solid #3a3d40; border-radius: 14px; padding: 12px; background: #181b1e; }}
img {{ width: 100%; image-rendering: pixelated; background: #000; border-radius: 10px; }}
h2 {{ font-size: 16px; margin: 10px 0 6px; }}
p {{ color: #bfc5c9; font-size: 13px; margin: 0; }}
</style>
<h1>Animaciones exportadas</h1>
<p>Para reordenar una animacion, crea un <code>order.txt</code> en su carpeta fuente con un nombre de imagen por linea y vuelve a exportar.</p>
<section class="grid">
{''.join(items)}
</section>
</html>
""", encoding="utf-8")


def write_local_manifest(output_root: Path, rows: list[dict]) -> None:
    files = []
    for row in rows:
        anim_path = row["anim_path"]
        files.append({
            "path": anim_path.relative_to(output_root).as_posix(),
            "category": row["category"],
            "name": row["name"],
            "bytes": anim_path.stat().st_size,
            "frames": row["frames"],
        })
    files.sort(key=lambda item: (item["category"], item["name"]))
    manifest = {
        "root": str(WORKSPACE_ROOT) + "\\",
        "faces": "faces_sd_anim/faces",
        "generated_by": "face_anim_tool.py",
        "files": files,
    }
    (output_root / "local_faces_manifest.json").write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2),
        encoding="utf-8",
    )


def add_export_row(rows: list[dict], category: str, name: str, frames: list[Frame],
    canvas: tuple[int, int], raw_bytes: int, anim_bytes: int, anim_path: Path, preview: Path) -> None:
    middle = frames[len(frames) // 2]
    rgb565_to_png(middle, canvas, preview)
    rows.append({
        "category": category,
        "name": name,
        "frames": len(frames),
        "raw_kb": raw_bytes / 1024,
        "anim_kb": anim_bytes / 1024,
        "anim_path": anim_path,
        "preview": preview,
    })


def export_from_manifest(args: argparse.Namespace) -> None:
    variants = load_variants(args.manifest)
    faces_root = prepare_output(args.output, args.clean)

    rows = []
    total_raw = 0
    total_anim = 0
    for category, keys in CATEGORY_VARIANTS.items():
        for key in keys:
            variant = variants.get(key)
            if variant is None:
                print(f"missing {key}")
                continue
            frames = [frame_from_manifest(args.raw_root, frame) for frame in variant["frames"]]
            name = safe_name(key)
            anim_path = faces_root / category / f"{name}.anim"
            raw_bytes, anim_bytes = write_anim(anim_path, frames, tuple(args.canvas), args.fps)
            total_raw += raw_bytes
            total_anim += anim_bytes
            preview = args.output / "_preview" / category / f"{name}.png"
            preview.parent.mkdir(parents=True, exist_ok=True)
            add_export_row(rows, category, name, frames, tuple(args.canvas),
                raw_bytes, anim_bytes, anim_path, preview)

    write_preview(args.output, rows)
    write_local_manifest(args.output, rows)
    print(f"Wrote {faces_root}")
    print(f"Animations: {len(rows)}")
    print(f"Raw RGB565: {total_raw / (1024 * 1024):.1f} MB")
    print(f"ANIM RLE: {total_anim / (1024 * 1024):.1f} MB")
    print(f"Preview: {args.output / '_preview.html'}")


def export_from_images(args: argparse.Namespace) -> None:
    faces_root = prepare_output(args.output, args.clean)

    rows = []
    total_raw = 0
    total_anim = 0
    canvas = tuple(args.canvas)

    category_dirs = [p for p in args.input.iterdir() if p.is_dir()]
    if args.category:
        category_dirs = [args.input]

    for category_dir in category_dirs:
        category = args.category or safe_name(category_dir.name)
        animation_dirs = [p for p in category_dir.iterdir() if p.is_dir()]
        if ordered_image_files(category_dir):
            animation_dirs = [category_dir]
        for animation_dir in animation_dirs:
            files = ordered_image_files(animation_dir)
            if not files:
                continue
            frames = [image_to_frame(path, canvas) for path in files]
            name = safe_name(animation_dir.name)
            anim_path = faces_root / category / f"{name}.anim"
            raw_bytes, anim_bytes = write_anim(anim_path, frames, canvas, args.fps)
            total_raw += raw_bytes
            total_anim += anim_bytes
            preview = args.output / "_preview" / category / f"{name}.png"
            preview.parent.mkdir(parents=True, exist_ok=True)
            add_export_row(rows, category, name, frames, canvas,
                raw_bytes, anim_bytes, anim_path, preview)

    write_preview(args.output, rows)
    write_local_manifest(args.output, rows)
    print(f"Wrote {faces_root}")
    print(f"Animations: {len(rows)}")
    print(f"Raw RGB565: {total_raw / (1024 * 1024):.1f} MB")
    print(f"ANIM RLE: {total_anim / (1024 * 1024):.1f} MB")
    print(f"Preview: {args.output / '_preview.html'}")


def export_from_caritas(args: argparse.Namespace) -> None:
    faces_root = prepare_output(args.output, args.clean)
    rows = []
    total_raw = 0
    total_anim = 0
    canvas = tuple(args.canvas)
    source_count = 0

    for expression, name, folder in iter_caritas_animation_dirs(args.input):
        files = ordered_image_files(folder)
        if not files:
            continue
        frames = [image_to_frame(path, canvas) for path in files]
        source_count += 1
        for category in caritas_categories(expression):
            anim_path = faces_root / category / f"{name}.anim"
            raw_bytes, anim_bytes = write_anim(anim_path, frames, canvas, args.fps)
            total_raw += raw_bytes
            total_anim += anim_bytes
            preview = args.output / "_preview" / category / f"{name}.png"
            preview.parent.mkdir(parents=True, exist_ok=True)
            add_export_row(rows, category, name, frames, canvas,
                raw_bytes, anim_bytes, anim_path, preview)

    write_preview(args.output, rows)
    write_local_manifest(args.output, rows)
    print(f"Wrote {faces_root}")
    print(f"Source animations: {source_count}")
    print(f"Exported .anim files: {len(rows)}")
    print(f"Raw RGB565: {total_raw / (1024 * 1024):.1f} MB")
    print(f"ANIM RLE: {total_anim / (1024 * 1024):.1f} MB")
    print(f"Preview: {args.output / '_preview.html'}")


def add_common_args(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--fps", type=int, default=DEFAULT_FPS)
    parser.add_argument("--canvas", type=int, nargs=2, default=list(DEFAULT_CANVAS), metavar=("W", "H"))
    parser.add_argument("--clean", action=argparse.BooleanOptionalAction, default=True)


def main() -> None:
    parser = argparse.ArgumentParser(description="Export face animation folders to RADAR .anim files.")
    sub = parser.add_subparsers(dest="command", required=True)

    manifest = sub.add_parser("from-manifest", help="Export the current caritas_sd_rgb565 manifest to /faces/*.anim.")
    add_common_args(manifest)
    manifest.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST)
    manifest.add_argument("--raw-root", type=Path, default=DEFAULT_RAW_ROOT)
    manifest.set_defaults(func=export_from_manifest)

    images = sub.add_parser("from-images", help="Export image folders to /faces/<category>/<animation>.anim.")
    add_common_args(images)
    images.add_argument("input", type=Path)
    images.add_argument("--category", help="Use when input is one category folder or one animation folder.")
    images.set_defaults(func=export_from_images)

    caritas = sub.add_parser("from-caritas", help="Export every animation found in /caritas to firmware categories.")
    add_common_args(caritas)
    caritas.add_argument("input", type=Path, nargs="?", default=DEFAULT_CARITAS_ROOT)
    caritas.set_defaults(func=export_from_caritas)

    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
