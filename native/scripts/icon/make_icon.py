# The app icon: a Corinthian helmet (front view) in gold over a dark ember background, 1024x1024, no alpha.
# usage: python3 make_icon.py app/assets/icon.png
import math, random, sys
from PIL import Image, ImageDraw, ImageFilter, ImageChops

S = 4096  # drawn at 4x, scaled down for smooth edges
c = S // 2
def P(x, y): return (x * 4, y * 4)

# Background: radial ember glow.
bg = Image.new('RGB', (S, S))
px = Image.radial_gradient('L').resize((S, S))  # 0 center .. 255 edge
inner, outer = (120, 26, 18), (9, 6, 8)
bg = Image.composite(Image.new('RGB', (S, S), outer), Image.new('RGB', (S, S), inner), px.point(lambda v: min(255, int(v * 1.25))))
glow = Image.new('L', (S, S), 0)
ImageDraw.Draw(glow).ellipse([c - 1500, c - 1300, c + 1500, c + 1700], fill=90)
glow = glow.filter(ImageFilter.GaussianBlur(400))
bg = Image.composite(Image.new('RGB', (S, S), (220, 70, 30)), bg, glow)

# Embers.
rnd = random.Random(7)
d = ImageDraw.Draw(bg)
for _ in range(70):
    x, y = rnd.uniform(0, S), rnd.uniform(S * 0.45, S)
    r = rnd.uniform(5, 18)
    a = rnd.uniform(0.3, 1)
    d.ellipse([x - r, y - r, x + r, y + r], fill=(int(255 * a), int(150 * a), int(60 * a)))

def mask(draw_fn):
    m = Image.new('L', (S, S), 0)
    draw_fn(ImageDraw.Draw(m))
    return m

# Crest (behind the dome): a tall fan of red plume.
crest = mask(lambda g: g.ellipse([*P(400, 120), *P(624, 560)], fill=255))
crest_fill = Image.new('RGB', (S, S), (178, 34, 26))
stripes = mask(lambda g: [g.line([P(512 + dx, 130), P(512 + dx * 0.7, 520)], fill=255, width=14) for dx in range(-96, 97, 32)])
crest_fill = Image.composite(Image.new('RGB', (S, S), (120, 18, 14)), crest_fill, stripes)

# Helmet: dome + cheek guards, minus the T-shaped face opening.
def helmet(g):
    g.ellipse([*P(262, 300), *P(762, 800)], fill=255)
    g.polygon([P(262, 560), P(762, 560), P(740, 760), P(660, 900), P(590, 905), P(560, 760),
               P(464, 760), P(434, 905), P(364, 900), P(284, 760)], fill=255)
body = mask(helmet)
def opening(g):
    # eye band with swept ends, and one opening down the middle between the cheek guards
    g.polygon([P(318, 585), P(470, 560), P(554, 560), P(706, 585), P(690, 640), P(574, 628), P(450, 628), P(334, 640)], fill=255)
    g.polygon([P(450, 600), P(574, 600), P(600, 905), P(424, 905)], fill=255)
hole = mask(opening)
body = ImageChops.subtract(body, hole)

# Nose guard hanging into the opening.
nose = mask(lambda g: g.polygon([P(494, 560), P(530, 560), P(526, 735), P(512, 752), P(498, 735)], fill=255))
body = ImageChops.lighter(body, nose)
hole = ImageChops.subtract(hole, nose)

# Gold: vertical gradient plus a soft highlight on the dome.
grad = Image.linear_gradient('L').resize((S, S))
gold_top, gold_bottom = (250, 222, 140), (158, 104, 30)
gold = Image.composite(Image.new('RGB', (S, S), gold_bottom), Image.new('RGB', (S, S), gold_top), grad)
shine = mask(lambda g: g.ellipse([*P(330, 330), *P(560, 520)], fill=120)).filter(ImageFilter.GaussianBlur(120))
gold = Image.composite(Image.new('RGB', (S, S), (255, 245, 210)), gold, shine)
# Ridge down the dome's center.
ridge = mask(lambda g: g.line([P(512, 305), P(512, 560)], fill=255, width=36))
gold = Image.composite(Image.new('RGB', (S, S), (196, 140, 50)), gold, ridge)

# Shadow under the helmet.
shadow = body.filter(ImageFilter.GaussianBlur(70))
out = Image.composite(Image.new('RGB', (S, S), (0, 0, 0)), bg, shadow.point(lambda v: int(v * 0.7)).transform((S, S), Image.AFFINE, (1, 0, 0, 0, 1, -60)))
out = Image.composite(crest_fill, out, crest)
# Dark edge, then the gold body.
edge = body.filter(ImageFilter.MaxFilter(25))
out = Image.composite(Image.new('RGB', (S, S), (40, 20, 10)), out, edge)
out = Image.composite(gold, out, body)
# The opening shows a dark interior.
inside = ImageChops.multiply(hole, mask(lambda g: g.ellipse([*P(262, 300), *P(762, 940)], fill=255)))
out = Image.composite(Image.new('RGB', (S, S), (14, 8, 8)), out, inside)

out = out.resize((1024, 1024), Image.LANCZOS)
out.save(sys.argv[1])
