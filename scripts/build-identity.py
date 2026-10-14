"""Build small pixel masters, static paper grain and the Windows icon (Pillow)."""
from pathlib import Path
from PIL import Image, ImageDraw
import random
ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'assets/identity'
# Deliberately pixel-drawn silhouettes, never a reduced illustration.
for size in (16, 24, 32):
    image = Image.new('RGBA', (size, size))
    draw = ImageDraw.Draw(image)
    c = size // 2
    dark, wing, edge, red = '#292823', '#b9b5a7', '#e7e1d3', '#e0261c'
    radius = {16: 1, 24: 2, 32: 3}[size]
    for side in (-1, 1):
        draw.line([(c + side, size//3), (c + side*(size//3), size//5)], fill=edge)
        draw.line([(c, size//2), (c + side*(size//3), size//2+2)], fill=edge)
        draw.line([(c, size*2//3), (c + side*(size//4), size-2)], fill=edge)
        draw.polygon([(c+side, size//3), (c+side*(size//3), size//2), (c+side*(size//3), size*3//4), (c+side, size*2//3)], fill=wing, outline=edge)
    draw.rectangle((c-radius, size//3, c+radius, size*3//4), fill=dark)
    draw.rectangle((c-radius+1, size*3//4, c+radius-1, size-3), fill=dark)
    for side in (-1, 1):
        x = c+side*radius
        draw.rectangle((x-radius+1, size//4-radius, x+radius-1, size//4+radius), fill=red)
    image.save(OUT / f'pixel-{size}.png')
    # SVG master uses exactly the same integer grid, with no filters.
    rects = []
    for y in range(size):
        for x in range(size):
            r,g,b,a = image.getpixel((x,y))
            if a: rects.append(f'<path fill="#{r:02x}{g:02x}{b:02x}" d="M{x} {y}h1v1h-1z"/>')
    (OUT / f'pixel-{size}.svg').write_text(f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {size} {size}" shape-rendering="crispEdges">'+''.join(rects)+'</svg>\n')
random.seed(10)
grain = Image.new('RGBA', (128,128))
for y in range(128):
    for x in range(128): grain.putpixel((x,y),(55,45,25,random.choice([0,0,0,2,3,5])))
grain.save(OUT / 'paper-grain.png')
# Pillow's append_images preserves the manually drawn small sizes.
Image.open(OUT/'icon-256.png').save(ROOT/'build/icon.ico', sizes=[(n,n) for n in (16,24,32,48,64,128,256)], append_images=[Image.open(OUT/f'pixel-{n}.png') for n in (16,24,32)]+[Image.open(OUT/f'icon-{n}.png') for n in (48,64,128)])
# Linux packages need one PNG per hicolor size: a lone 1024 px icon is not in
# the theme's size list, so menus and AppImage launchers fall back to a generic one.
(ROOT/'build/icons').mkdir(exist_ok=True)
for n in (16, 24, 32, 48, 64, 128, 256, 512):
    source = OUT/(f'pixel-{n}.png' if n <= 32 else f'icon-{n}.png')
    (ROOT/f'build/icons/{n}x{n}.png').write_bytes(source.read_bytes())
