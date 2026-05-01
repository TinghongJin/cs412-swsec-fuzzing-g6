from PIL import Image
import os

out = "seeds"
os.makedirs(out, exist_ok=True)

# 1. Grayscale
img = Image.new("L", (32, 32), color=120)
img.save(f"{out}/grayscale.png")

# 2. RGB
img = Image.new("RGB", (32, 32), color=(255, 0, 0))
img.save(f"{out}/rgb.png")

# 3. RGBA
img = Image.new("RGBA", (32, 32), color=(0, 255, 0, 128))
img.save(f"{out}/rgba.png")

# 4. Palette (PLTE) ⭐
img = Image.new("P", (32, 32))
palette = []
for i in range(256):
    palette.extend([i, 255 - i, (i * 3) % 255])
img.putpalette(palette)

pixels = [i % 256 for i in range(32 * 32)]
img.putdata(pixels)
img.save(f"{out}/palette.png")

# 5. Interlaced PNG
img = Image.new("RGB", (32, 32), color=(0, 0, 255))
img.save(f"{out}/interlaced.png", interlace=1)

# 6. 16-bit depth PNG
img = Image.new("I;16", (32, 32))
img.save(f"{out}/depth16.png")

print("Done generating seeds")