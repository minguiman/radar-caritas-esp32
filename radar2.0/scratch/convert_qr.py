import sys
try:
    from PIL import Image
except ImportError:
    import subprocess
    subprocess.check_call([sys.executable, "-m", "pip", "install", "Pillow"])
    from PIL import Image

def rgb888_to_rgb565(r, g, b):
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)

img = Image.open('QR.png').convert('RGB')
width, height = img.size

out = f"#pragma once\n\n#include <stdint.h>\n\n"
out += f"constexpr int kQrWidth = {width};\n"
out += f"constexpr int kQrHeight = {height};\n\n"
out += f"constexpr uint16_t kQrImage[] = {{\n"

pixels = img.load()
for y in range(height):
    out += "    "
    for x in range(width):
        r, g, b = pixels[x, y]
        rgb565 = rgb888_to_rgb565(r, g, b)
        # LVGL might use big endian or little endian? 
        # Actually, in RadarRenderer it just writes it.
        # Wait, the display uses swap_bytes or not? I should swap bytes for LV_COLOR_16_SWAP if needed, but let's just write raw uint16_t first.
        # Actually, the user's platformio.ini has -DLV_COLOR_16_SWAP=0. So just normal host uint16_t is fine.
        out += f"0x{rgb565:04X}, "
    out += "\n"

out = out.rstrip(", \n") + "\n};\n"

with open('src/qr_image.h', 'w') as f:
    f.write(out)

print(f"Converted QR.png ({width}x{height}) to src/qr_image.h")
