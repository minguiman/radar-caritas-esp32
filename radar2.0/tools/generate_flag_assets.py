#!/usr/bin/env python3
"""Genera CountryFlags.h/cpp desde PNGs o descargando de flagcdn.com."""

from __future__ import annotations

import argparse
import struct
import sys
import urllib.request
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    print("Instala Pillow: pip install pillow", file=sys.stderr)
    sys.exit(1)

FLAG_W = 32
FLAG_H = 22
TRANSPARENT_KEY = 0xF81F

# Códigos ISO usados por CountryResolver (IcaoRange + prefijos registro).
COUNTRY_CODES = sorted({
    "ae", "af", "ag", "al", "am", "ao", "ar", "at", "au", "az", "ba", "bb", "bd", "be", "bf",
    "bg", "bh", "bi", "bj", "bm", "bn", "bo", "br", "bs", "bt", "by", "bz", "ca", "cd", "cf",
    "cg", "ch", "ci", "ck", "cl", "cm", "cn", "co", "cr", "cu", "cv", "cy", "cz", "de", "dj",
    "do", "dz", "ec", "eg", "er", "es", "et", "fi", "fj", "fm", "fr", "gb", "gd", "ge", "gg",
    "gh", "gm", "gq", "gr", "gt", "gw", "gy", "hk", "hn", "hr", "ht", "hu", "id", "ie", "il",
    "im", "in", "iq", "ir", "is", "it", "jm", "jo", "jp", "ke", "kg", "kh", "ki", "km", "kr",
    "kw", "ky", "kz", "la", "lb", "lc", "lk", "lr", "ls", "lt", "lu", "lv", "ly", "ma", "mc",
    "md", "me", "mg", "mh", "mk", "ml", "mm", "mn", "mr", "mt", "mu", "mv", "mw", "mx", "my",
    "mz", "na", "ne", "ng", "ni", "nl", "no", "np", "nr", "nz", "om", "pa", "pe", "pg", "ph",
    "pk", "pl", "pt", "pw", "py", "qa", "ro", "rs", "ru", "rw", "sa", "sb", "sc", "sd", "se",
    "sg", "si", "sk", "sl", "sm", "sn", "so", "sr", "st", "sv", "sz", "tc", "td", "tg", "th",
    "tj", "tm", "to", "tr", "tt", "tw", "tz", "ua", "ug", "us", "uy", "uz", "vc", "ve", "vn",
    "vu", "ws", "ye", "za", "zm", "zw", "xy",
})


def rgb_to_565(r: int, g: int, b: int) -> int:
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def load_flag_png(path: Path) -> list[int]:
    img = Image.open(path).convert("RGBA").resize((FLAG_W, FLAG_H), Image.Resampling.BILINEAR)
    pixels: list[int] = []
    for y in range(FLAG_H):
        for x in range(FLAG_W):
            r, g, b, a = img.getpixel((x, y))
            if a < 32:
                pixels.append(TRANSPARENT_KEY)
            else:
                pixels.append(rgb_to_565(r, g, b))
    return pixels


def download_flag(code: str, dest: Path) -> bool:
    url = f"https://flagcdn.com/w80/{code}.png"
    try:
        with urllib.request.urlopen(url, timeout=15) as resp:
            dest.write_bytes(resp.read())
        return True
    except Exception as exc:
        print(f"WARN: no se pudo descargar {code}: {exc}")
        return False


def make_unknown_flag() -> list[int]:
    # Gris liso (sin patrón ajedrez que parece fondo PNG transparente).
    fill = rgb_to_565(72, 76, 84)
    border = rgb_to_565(96, 100, 108)
    pixels: list[int] = []
    for y in range(FLAG_H):
        for x in range(FLAG_W):
            if x < 1 or y < 1 or x >= FLAG_W - 1 or y >= FLAG_H - 1:
                pixels.append(border)
            else:
                pixels.append(fill)
    return pixels


def emit_cpp(codes: list[str], pixel_map: dict[str, list[int]], out_h: Path, out_cpp: Path) -> None:
    h = [
        "#pragma once",
        "",
        "#include \"PlaneData.h\"",
        "",
        "#include <cstdint>",
        "",
        "namespace CountryFlags",
        "{",
        f"inline constexpr uint8_t kFlagWidth = {FLAG_W};",
        f"inline constexpr uint8_t kFlagHeight = {FLAG_H};",
        f"inline constexpr uint16_t kTransparentKey = 0x{TRANSPARENT_KEY:04X};",
        "",
        "struct FlagImage",
        "{",
        "    const char* code;",
        "    const uint16_t* pixels;",
        "};",
        "",
        "const FlagImage* flagForCountryCode(const char* code);",
        "const FlagImage* flagForPlane(const Plane& plane);",
        "}",
    ]
    out_h.write_text("\n".join(h) + "\n", encoding="ascii")

    lines = [
        '#include "CountryFlags.h"',
        "",
        '#include "CountryResolver.h"',
        "",
        "namespace CountryFlags",
        "{",
    ]
    for code in codes:
        arr_name = f"kFlag_{code}"
        pixels = pixel_map[code]
        lines.append(f"static const uint16_t {arr_name}[{len(pixels)}] = {{")
        row = []
        for i, px in enumerate(pixels):
            row.append(f"0x{px:04X}")
            if len(row) == 12:
                lines.append("    " + ", ".join(row) + ",")
                row = []
        if row:
            lines.append("    " + ", ".join(row) + ",")
        lines.append("};")
        lines.append("")

    lines.append("static const FlagImage kFlags[] = {")
    for code in codes:
        lines.append(f'    {{"{code}", kFlag_{code}}},')
    lines.append("};")
    lines.append("")
    lines.append("static char lowerAscii(char c)")
    lines.append("{")
    lines.append("    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + ('a' - 'A')) : c;")
    lines.append("}")
    lines.append("")
    lines.append("const FlagImage* flagForCountryCode(const char* code)")
    lines.append("{")
    lines.append("    if (code == nullptr || code[0] == '\\0' || code[1] == '\\0') {")
    lines.append('        return &kFlags[0];')
    lines.append("    }")
    lines.append("    char normalized[3] = {")
    lines.append("        lowerAscii(code[0]),")
    lines.append("        lowerAscii(code[1]),")
    lines.append("        '\\0'};")
    lines.append("    for (const FlagImage& flag : kFlags) {")
    lines.append("        if (flag.code[0] == normalized[0] && flag.code[1] == normalized[1]) {")
    lines.append("            return &flag;")
    lines.append("        }")
    lines.append("    }")
    lines.append('    return &kFlags[0];')
    lines.append("}")
    lines.append("")
    lines.append("const FlagImage* flagForPlane(const Plane& plane)")
    lines.append("{")
    lines.append("    const CountryResolver::CountryInfo country = CountryResolver::resolveCountry(plane);")
    lines.append("    return flagForCountryCode(country.code);")
    lines.append("}")
    lines.append("}")
    out_cpp.write_text("\n".join(lines) + "\n", encoding="ascii")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--src-dir", type=Path, default=Path(__file__).resolve().parents[1] / "src")
    parser.add_argument("--flags-dir", type=Path, default=Path(__file__).resolve().parents[1] / "assets" / "flags")
    parser.add_argument("--download", action="store_true", help="Descargar PNGs faltantes de flagcdn")
    args = parser.parse_args()

    args.flags_dir.mkdir(parents=True, exist_ok=True)
    pixel_map: dict[str, list[int]] = {}

    codes = ["xy"] + [c for c in COUNTRY_CODES if c != "xy"]
    for code in codes:
        png_path = args.flags_dir / f"{code}.png"
        if not png_path.exists() and args.download:
            download_flag(code, png_path)
        if png_path.exists():
            pixel_map[code] = load_flag_png(png_path)
        elif code == "xy":
            pixel_map[code] = make_unknown_flag()
        else:
            print(f"SKIP {code}: sin PNG (usa --download)")

    # Rellenar faltantes con bandera desconocida
    unknown = pixel_map.get("xy", make_unknown_flag())
    for code in codes:
        if code not in pixel_map:
            pixel_map[code] = list(unknown)

    emit_cpp(codes, pixel_map, args.src_dir / "CountryFlags.h", args.src_dir / "CountryFlags.cpp")
    print(f"Generados {len(codes)} banderas en {args.src_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
