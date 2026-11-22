"""Procedural placeholder art for the sprite pipeline: ink wash on paper.

Writes assets/sprites/<name>.png for every placeholder sprite in art/sprites.toml: a painted ground, rocks, a pine,
a shrine, a fox, a cultivator, a villager, glowing orbs, a blob shadow, four world-map cells, the world map's road,
bridge, ford and marker overlays, its painted stamps, mist, places and clouds, and the abilities' effects: a sword, a
halo and a soft dot (both unused now), a fire bolt, the lance's burst and the bars' solid square. Each is original and
drawn from code. Reproducible byte for byte: the drawing uses only arithmetic and square roots (correctly rounded
everywhere), integer hashing, and seeded random.Random, never a transcendental function. Run through
`./dev atlas --placeholders`.

Standard library only, like tools/project_n.py, so it runs in the Pixi environment as it is.
"""

import itertools
import math
import random
import struct
import sys
import zlib
from pathlib import Path

OUT = Path(__file__).resolve().parent.parent / "assets" / "sprites"

PAPER = (232, 222, 196)
INK = (38, 42, 50)
MOSS = (88, 108, 88)
PINE = (44, 62, 56)
VERMILION = (164, 56, 42)


# Noise and shaping: integer hashing and polynomials only.

def hash01(x, y, seed):
    """A value in [0, 1) for integer lattice point (x, y)."""
    h = (x * 374761393 + y * 668265263 + seed * 2147483647) & 0xFFFFFFFF
    h = ((h ^ (h >> 13)) * 1274126177) & 0xFFFFFFFF
    return ((h ^ (h >> 16)) & 0xFFFFFF) / 16777216.0


def smooth(t):
    return t * t * (3.0 - 2.0 * t)


def smoothstep(edge0, edge1, x):
    t = (x - edge0) / (edge1 - edge0)
    return smooth(max(0.0, min(t, 1.0)))


def noise(x, y, seed):
    """Value noise in [0, 1), smooth between lattice points."""
    xi, yi = math.floor(x), math.floor(y)
    tx, ty = smooth(x - xi), smooth(y - yi)
    a, b = hash01(xi, yi, seed), hash01(xi + 1, yi, seed)
    c, d = hash01(xi, yi + 1, seed), hash01(xi + 1, yi + 1, seed)
    top, bottom = a + (b - a) * tx, c + (d - c) * tx
    return top + (bottom - top) * ty


def fbm(x, y, seed, octaves=4):
    total, amplitude, norm = 0.0, 1.0, 0.0
    for octave in range(octaves):
        total += noise(x, y, seed + octave * 101) * amplitude
        norm += amplitude
        x, y, amplitude = x * 2.0, y * 2.0, amplitude * 0.5
    return total / norm


def mix(a, b, t):
    return tuple(p + (q - p) * t for p, q in zip(a, b))


# A canvas of premultiplied RGBA floats, painted with "over".

class Canvas:
    def __init__(self, width, height, fill=None):
        self.width, self.height = width, height
        size = width * height
        colour = fill or (0, 0, 0)
        alpha = 1.0 if fill else 0.0
        self.r = [colour[0] / 255.0 * alpha] * size
        self.g = [colour[1] / 255.0 * alpha] * size
        self.b = [colour[2] / 255.0 * alpha] * size
        self.a = [alpha] * size

    def over(self, i, colour, alpha):
        keep = 1.0 - alpha
        self.r[i] = colour[0] / 255.0 * alpha + self.r[i] * keep
        self.g[i] = colour[1] / 255.0 * alpha + self.g[i] * keep
        self.b[i] = colour[2] / 255.0 * alpha + self.b[i] * keep
        self.a[i] = alpha + self.a[i] * keep

    def shape(self, box, coverage):
        """Paints every pixel of box (x0, y0, x1, y1) with coverage(x, y) -> (colour, alpha) or None."""
        x0, y0, x1, y1 = (max(0, int(box[0])), max(0, int(box[1])),
                          min(self.width, int(box[2]) + 1), min(self.height, int(box[3]) + 1))
        for y in range(y0, y1):
            for x in range(x0, x1):
                painted = coverage(x + 0.5, y + 0.5)
                if painted and painted[1] > 0.0:
                    self.over(y * self.width + x, painted[0], min(1.0, painted[1]))

    def dab(self, cx, cy, radius, colour, alpha, soft=0.6, dry=0.0, seed=0):
        """A round brush dab; `dry` leaves speckled gaps like a dry brush."""
        def coverage(x, y):
            d = math.sqrt((x - cx) ** 2 + (y - cy) ** 2)
            if d >= radius:
                return None
            cover = smoothstep(0.0, radius * soft, radius - d)
            if dry:
                cover *= 1.0 - dry * hash01(int(x), int(y), seed)
            return colour, alpha * cover
        self.shape((cx - radius, cy - radius, cx + radius, cy + radius), coverage)

    def stroke(self, points, width0, width1, colour, alpha, dry=0.0, seed=0):
        """A brush stroke along a polyline, tapering from width0 to width1."""
        lengths = [0.0]
        for (ax, ay), (bx, by) in itertools.pairwise(points):
            lengths.append(lengths[-1] + math.sqrt((bx - ax) ** 2 + (by - ay) ** 2))
        total = lengths[-1] or 1.0
        for (ax, ay), (bx, by), start, end in zip(points, points[1:], lengths, lengths[1:]):
            steps = max(1, int((end - start) / max(1.0, min(width0, width1) * 0.25)))
            for step in range(steps):
                t = step / steps
                along = (start + (end - start) * t) / total
                width = width0 + (width1 - width0) * along
                self.dab(ax + (bx - ax) * t, ay + (by - ay) * t, width * 0.5, colour, alpha, 0.5, dry, seed + step)

    def outline(self, colour, strength=0.85):
        """Inks the silhouette's edge: where a pixel is more opaque than its faintest neighbour."""
        w, h, a = self.width, self.height, self.a[:]
        for y in range(h):
            for x in range(w):
                i = y * w + x
                if a[i] < 0.05:
                    continue
                faintest = min(a[(y + dy) * w + x + dx] if 0 <= x + dx < w and 0 <= y + dy < h else 0.0
                               for dy in (-1, 0, 1) for dx in (-1, 0, 1))
                edge = a[i] - faintest
                if edge > 0.0:
                    ink = min(1.0, edge * 1.6) * strength
                    keep = 1.0 - ink
                    self.r[i] = colour[0] / 255.0 * ink * a[i] + self.r[i] * keep
                    self.g[i] = colour[1] / 255.0 * ink * a[i] + self.g[i] * keep
                    self.b[i] = colour[2] / 255.0 * ink * a[i] + self.b[i] * keep

    def png(self, path, opaque=False):
        """Writes straight (not premultiplied) 8-bit RGBA, or RGB when opaque."""
        channels = 3 if opaque else 4
        rows = []
        for y in range(self.height):
            row = bytearray(self.width * channels)
            for x in range(self.width):
                i = y * self.width + x
                alpha = self.a[i]
                if alpha <= 0.0:
                    continue  # transparent black
                pixel = (self.r[i] / alpha, self.g[i] / alpha, self.b[i] / alpha, alpha)[:channels]
                row[x * channels:(x + 1) * channels] = bytes(min(255, int(v * 255.0 + 0.5)) for v in pixel)
            # PNG's Sub filter: each byte minus the one a pixel to its left; smooth paint compresses better.
            filtered = bytes((row[i] - (row[i - channels] if i >= channels else 0)) & 0xFF for i in range(len(row)))
            rows.append(b"\x01" + filtered)

        def chunk(kind, data):
            return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
        header = struct.pack(">IIBBBBB", self.width, self.height, 8, 2 if opaque else 6, 0, 0, 0)
        path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header)
                         + chunk(b"IDAT", zlib.compress(b"".join(rows), 9)) + chunk(b"IEND", b""))


# Paper and ground.

def paper(width, height, seed, wash=0.25, vignette=0.0):
    """Warm paper with fibres and grain, ink washes pooled in blotches, darker toward the edges."""
    canvas = Canvas(width, height)
    # The washes vary slowly: sampled every 4 pixels and interpolated, so a 1080p ground draws in seconds.
    step = 4
    gw, gh = width // step + 2, height // step + 2
    grid = [fbm(gx * step / 380.0, gy * step / 380.0, seed) for gy in range(gh) for gx in range(gw)]
    moss = [fbm(gx * step / 260.0 + 9.0, gy * step / 260.0, seed + 7) for gy in range(gh) for gx in range(gw)]
    cx, cy = width / 2.0, height / 2.0
    for y in range(height):
        gy, ty = y // step, (y % step) / step
        for x in range(width):
            gx, tx = x // step, (x % step) / step
            i0, i1 = gy * gw + gx, (gy + 1) * gw + gx
            w = (grid[i0] * (1 - tx) + grid[i0 + 1] * tx) * (1 - ty) + (grid[i1] * (1 - tx) + grid[i1 + 1] * tx) * ty
            m = (moss[i0] * (1 - tx) + moss[i0 + 1] * tx) * (1 - ty) + (moss[i1] * (1 - tx) + moss[i1 + 1] * tx) * ty
            grain = (hash01(x, y, seed) - 0.5) * 0.035 + (noise(x / 48.0, y / 2.5, seed + 3) - 0.5) * 0.03
            colour = mix(PAPER, MOSS, smoothstep(0.5, 0.78, m) * wash * 0.6)
            colour = mix(colour, INK, smoothstep(0.46, 0.8, w) * wash)
            if vignette:
                dx, dy = (x - cx) / cx, (y - cy) / cy
                colour = mix(colour, INK, smoothstep(0.55, 1.5, dx * dx + dy * dy) * vignette)
            i = y * width + x
            canvas.r[i] = max(0.0, min(1.0, colour[0] / 255.0 + grain))
            canvas.g[i] = max(0.0, min(1.0, colour[1] / 255.0 + grain))
            canvas.b[i] = max(0.0, min(1.0, colour[2] / 255.0 + grain))
            canvas.a[i] = 1.0
    return canvas


def grass(canvas, rng, count, box, colour=INK, alpha=0.35, size=1.0):
    """Short dry strokes of grass in tufts."""
    for _ in range(count):
        x, y = rng.uniform(box[0], box[2]), rng.uniform(box[1], box[3])
        for blade in range(rng.randint(2, 4)):
            lean = rng.uniform(-6, 6) * size
            height = rng.uniform(8, 16) * size
            base = x + blade * 3 * size
            canvas.stroke([(base, y), (base + lean * 0.5, y - height * 0.6), (base + lean, y - height)],
                          2.2 * size, 0.6 * size, colour, alpha, dry=0.3, seed=rng.randint(0, 1 << 20))


def ground():
    canvas = paper(1920, 1080, 11, wash=0.32, vignette=0.35)
    rng = random.Random(1)
    grass(canvas, rng, 160, (40, 40, 1880, 1040), size=1.8)
    for _ in range(9):  # cracks in the earth
        x, y = rng.uniform(150, 1770), rng.uniform(120, 960)
        points = [(x, y)]
        for _ in range(rng.randint(4, 8)):
            x, y = x + rng.uniform(10, 34), y + rng.uniform(-14, 14)
            points.append((x, y))
        canvas.stroke(points, 3.0, 0.8, INK, 0.3, dry=0.4, seed=rng.randint(0, 1 << 20))
    return canvas


# Props.

def rock(width, height, seed):
    canvas = Canvas(width, height)
    cx, cy = width / 2.0, height * 0.55
    rx, ry = width * 0.48, height * 0.5
    edge = 2.0 / rx

    def body(x, y):
        dx, dy = (x - cx) / rx, (y - cy) / ry
        d = math.sqrt(dx * dx + dy * dy)
        if d == 0.0:
            d = 1e-6
        # The outline wobbles with noise read on the unit circle: no angle, so no atan2.
        radius = 0.78 + 0.22 * fbm(dx / d * 1.6 + 4.0, dy / d * 1.6 + 4.0, seed, 3)
        if dy > 0.0:
            radius *= 1.0 - 0.35 * dy  # a flatter base
        cover = smoothstep(radius, radius - edge, d)
        if cover <= 0.0:
            return None
        shade = smoothstep(-0.9, 0.9, dy + 0.4 * dx)
        colour = mix((168, 166, 158), (66, 68, 74), shade)
        strata = fbm(x / 9.0, y / 26.0, seed + 5, 3)
        colour = mix(colour, INK, smoothstep(0.55, 0.8, strata) * 0.45 + smoothstep(0.7, 1.0, d / radius) * 0.5)
        return colour, cover
    canvas.shape((0, 0, width, height), body)
    rng = random.Random(seed)
    for _ in range(4):  # cracks across the face
        x, y = rng.uniform(cx - rx * 0.5, cx + rx * 0.3), rng.uniform(cy - ry * 0.4, cy + ry * 0.2)
        canvas.stroke([(x, y), (x + rng.uniform(8, 20), y + rng.uniform(6, 18))], 2.4, 0.6, INK, 0.5, 0.3, seed)
    for _ in range(int(width / 6)):  # moss on top
        x = rng.uniform(cx - rx * 0.6, cx + rx * 0.6)
        canvas.dab(x, cy - ry * 0.55 + rng.uniform(-6, 10) + abs(x - cx) / rx * ry * 0.3,
                   rng.uniform(2.5, 5.0), MOSS, 0.5, dry=0.3, seed=seed)
    canvas.outline(INK)
    return canvas


def pine():
    canvas = Canvas(240, 380)
    rng = random.Random(3)
    trunk = [(120, 372), (116, 300), (122, 220), (114, 140), (118, 70)]
    canvas.stroke(trunk, 16, 5, (72, 58, 48), 0.95, dry=0.2, seed=5)
    tiers = [(300, 104), (250, 92), (200, 78), (152, 62), (108, 46), (70, 28)]
    for tier, (y, half) in enumerate(tiers):
        for side in (-1, 1):  # branches under the needles
            canvas.stroke([(120, y + 6), (120 + side * half * 0.5, y + 2), (120 + side * half * 0.9, y - 4)],
                          5, 1.5, (60, 50, 42), 0.8, seed=tier)
        for _ in range(int(half * 1.8)):
            x = 120 + rng.uniform(-half, half)
            dy = rng.uniform(-18, 10) * (1.0 - abs(x - 120) / half * 0.5)
            canvas.dab(x, y + dy, rng.uniform(5, 11), PINE, 0.55, dry=0.35, seed=rng.randint(0, 1 << 20))
        for _ in range(int(half * 0.35)):  # light on the upper needles
            x = 120 + rng.uniform(-half * 0.8, half * 0.6)
            canvas.dab(x, y - rng.uniform(6, 16), rng.uniform(3, 6), (110, 132, 108), 0.4, dry=0.4,
                       seed=rng.randint(0, 1 << 20))
    canvas.outline(INK, 0.6)
    return canvas


def shrine():
    canvas = Canvas(220, 250)

    def box(x0, y0, x1, y1, colour, shade=0.0):
        canvas.shape((x0, y0, x1, y1), lambda x, y: (mix(colour, INK, shade * (y - y0) / (y1 - y0)), 1.0))
    box(30, 212, 190, 246, (150, 146, 136), 0.4)   # plinth
    box(46, 200, 174, 214, (170, 166, 156), 0.3)   # step
    box(64, 112, 156, 200, (52, 48, 50), 0.3)      # the dark interior
    for x in (60, 146):                            # pillars
        box(x, 108, x + 14, 202, VERMILION, 0.35)
    box(50, 98, 170, 112, (120, 44, 36), 0.2)      # lintel

    def roof(x, y):
        t = (x - 110.0) / 104.0
        if abs(t) > 1.0:
            return None
        bottom = 98.0 - 26.0 * t * t * t * t   # eaves sweep up at the ends
        top = 34.0 + 30.0 * t * t
        if not top <= y <= bottom:
            return None
        tiles = 0.25 if int(x) % 9 < 2 else 0.0
        return mix((92, 96, 104), INK, 0.35 + tiles + 0.3 * (y - top) / (bottom - top)), 1.0
    canvas.shape((0, 20, 220, 100), roof)
    for x in (8, 212):
        canvas.dab(x, 72, 5, INK, 0.9)             # ridge ends
    canvas.dab(110, 30, 6, VERMILION, 0.9)         # ridge jewel
    canvas.outline(INK)
    return canvas


# Creatures.

def fox():
    canvas = Canvas(112, 72)
    orange, white = (206, 118, 58), (238, 230, 214)

    def ellipse(cx, cy, rx, ry, colour):
        def inside(x, y):
            d = math.sqrt(((x - cx) / rx) ** 2 + ((y - cy) / ry) ** 2)
            return (colour, smoothstep(1.0, 1.0 - 1.5 / rx, d)) if d < 1.0 else None
        canvas.shape((cx - rx, cy - ry, cx + rx, cy + ry), inside)

    def triangle(a, b, c, colour):
        def inside(x, y):
            def side(p, q):
                return (q[0] - p[0]) * (y - p[1]) - (q[1] - p[1]) * (x - p[0])
            s = (side(a, b), side(b, c), side(c, a))
            return (colour, 1.0) if all(v >= 0 for v in s) or all(v <= 0 for v in s) else None
        canvas.shape((min(a[0], b[0], c[0]), min(a[1], b[1], c[1]), max(a[0], b[0], c[0]), max(a[1], b[1], c[1])), inside)

    canvas.stroke([(30, 42), (16, 36), (8, 24), (10, 12)], 14, 18, orange, 1.0)   # tail
    canvas.dab(10, 12, 8, white, 1.0)                                            # its tip
    for x in (34, 44, 62, 72):                                                   # legs
        canvas.stroke([(x, 50), (x + 1, 68)], 6, 5, (90, 52, 34), 1.0)
    ellipse(52, 44, 30, 15, orange)                                              # body
    ellipse(58, 50, 18, 7, white)                                                # belly
    triangle((78, 26), (82, 8), (90, 24), orange)                                # ears
    triangle((88, 24), (96, 9), (98, 28), orange)
    ellipse(88, 32, 13, 11, orange)                                              # head
    ellipse(99, 38, 10, 5, white)                                                # snout
    canvas.dab(108, 37, 2.5, INK, 1.0)                                           # nose
    canvas.dab(92, 29, 2.2, INK, 1.0)                                            # eye
    canvas.outline(INK)
    return canvas


def trapezoid(canvas, y0, y1, half0, half1, colour, shade, cx):
    """Cloth from `half0` wide at `y0` to `half1` at `y1` about `cx`, shaded towards its right edge."""
    def inside(x, y):
        t = (y - y0) / (y1 - y0)
        half = half0 + (half1 - half0) * t
        if abs(x - cx) > half:
            return None
        return mix(colour, shade, smoothstep(-half, half, x - cx) * 0.6), 1.0
    canvas.shape((cx - max(half0, half1), y0, cx + max(half0, half1), y1), inside)


def cultivator():
    canvas = Canvas(72, 128)
    robe, shade = (222, 228, 232), (150, 166, 186)
    trapezoid(canvas, 44, 122, 13, 26, robe, shade, 36.0)  # robe
    trapezoid(canvas, 46, 84, 8, 20, robe, shade, 18)      # sleeves
    trapezoid(canvas, 46, 84, 8, 20, robe, shade, 54)
    canvas.shape((22, 70, 50, 77), lambda x, y: (VERMILION, 1.0))  # sash
    canvas.stroke([(36, 46), (36, 120)], 1.5, 1.5, shade, 0.8)     # the robe's fold
    canvas.dab(36, 30, 11, (228, 202, 172), 1.0, soft=0.15)        # head
    canvas.dab(36, 22, 10, (34, 34, 40), 1.0, soft=0.15)           # hair
    canvas.dab(36, 11, 6, (34, 34, 40), 1.0, soft=0.2)             # bun
    canvas.outline(INK)
    return canvas


def villager():
    """An NPC: a short indigo tunic over dark trousers and a wide straw hat, unlike the player's white robe."""
    canvas = Canvas(64, 116)
    tunic, shade = (70, 92, 128), (36, 46, 70)
    for x in (25, 39):                                                             # trousers
        canvas.stroke([(x, 80), (x, 110)], 6, 5, (52, 48, 52), 1.0)
    trapezoid(canvas, 42, 86, 10, 18, tunic, shade, 32.0)                          # tunic
    trapezoid(canvas, 44, 74, 6, 11, tunic, shade, 16)                             # sleeves
    trapezoid(canvas, 44, 74, 6, 11, tunic, shade, 48)
    canvas.shape((19, 62, 45, 67), lambda x, y: ((120, 86, 52), 1.0))             # belt
    canvas.dab(32, 34, 9, (214, 184, 150), 1.0, soft=0.15)                         # head
    trapezoid(canvas, 10, 28, 2, 28, (200, 172, 108), (150, 120, 70), 32.0)        # straw hat
    canvas.outline(INK)
    return canvas


# Effects and helpers.

def orb(size, colour):
    """Additive glow: a white-hot core fading to colour; drawn with ONE, ONE blending."""
    canvas = Canvas(size, size)
    centre, radius = size / 2.0, size / 2.0

    def glow(x, y):
        d = math.sqrt((x - centre) ** 2 + (y - centre) ** 2) / radius
        if d >= 1.0:
            return None
        falloff = (1.0 - d) * (1.0 - d)
        return mix(colour, (255, 255, 255), smoothstep(0.45, 0.0, d)), falloff
    canvas.shape((0, 0, size, size), glow)
    return canvas


def shadow():
    canvas = Canvas(64, 24)

    def blob(x, y):
        d = math.sqrt(((x - 32) / 32) ** 2 + ((y - 12) / 12) ** 2)
        return (INK, 0.45 * smoothstep(1.0, 0.2, d)) if d < 1.0 else None
    canvas.shape((0, 0, 64, 24), blob)
    return canvas


# The abilities' effects. All but the sword are white, so the colour table's element colour tints them.

STEEL = (148, 160, 176)
EDGE = (236, 242, 248)


def sword():
    """A straight jian pointing up, its foot at its centre: a steel blade with a pale edge, a guard, a grip."""
    canvas = Canvas(10, 40)

    def blade(x, y):
        if not 1.0 <= y <= 29.0:
            return None
        half = 2.2 * math.sqrt(min(1.0, (y - 1.0) / 6.0))  # a point that widens to the blade over its first 6 pixels
        offset = x - 5.0
        if abs(offset) > half + 0.5:
            return None
        cover = max(0.0, min(1.0, half + 0.5 - abs(offset)))
        colour = mix(STEEL, EDGE, smoothstep(-0.5, half, offset))  # the right edge catches the light
        return mix(colour, INK, 0.25 * smoothstep(0.6, 0.0, abs(offset))), cover  # the ridge down its middle
    canvas.shape((0, 0, 10, 30), blade)
    canvas.shape((0, 29, 10, 32), lambda x, y: ((96, 84, 66), 1.0) if 0.5 <= x <= 9.5 else None)  # the guard
    canvas.shape((3, 32, 7, 38), lambda x, y: ((60, 44, 40), 1.0) if 3.8 <= x <= 6.2 else None)  # the grip
    canvas.dab(5.0, 38.0, 1.8, (96, 84, 66), 1.0, soft=0.3)  # the pommel
    canvas.outline(INK, 0.6)
    return canvas


def halo():
    """A soft round glow, white. Unused since the swords wear glows (core/glow.hpp): kept for its stable ID."""
    canvas = Canvas(48, 48)

    def glow(x, y):
        d = math.sqrt((x - 24.0) ** 2 + (y - 24.0) ** 2) / 24.0
        return ((255, 255, 255), 0.2 * (1.0 - d) * (1.0 - d)) if d < 1.0 else None
    canvas.shape((0, 0, 48, 48), glow)
    return canvas


def glow_core():
    """A small soft dot, white. Unused since the swords wear glows (core/glow.hpp): kept for its stable ID."""
    canvas = Canvas(16, 16)

    def dot(x, y):
        d = math.sqrt((x - 8.0) ** 2 + (y - 8.0) ** 2) / 8.0
        return ((255, 255, 255), smoothstep(1.0, 0.35, d)) if d < 1.0 else None
    canvas.shape((0, 0, 16, 16), dot)
    return canvas


def fire_bolt():
    """A teardrop pointing up, white with a denser core, its foot at its centre: tinted by its element."""
    canvas = Canvas(24, 48)

    def drop(x, y):
        dx = x - 12.0
        below = math.sqrt(dx * dx + (y - 34.0) ** 2) / 10.0  # the round head at the bottom
        if y < 34.0:
            half = 10.0 * math.sqrt(max(0.0, (y - 2.0) / 32.0))  # narrowing to the point at the top
            across = abs(dx) / half if half > 0.0 else 2.0
            d = max(across, 0.0) if y >= 2.0 else 2.0
        else:
            d = below
        if d >= 1.0:
            return None
        return (255, 255, 255), smoothstep(1.0, 0.55, d) * (0.65 + 0.35 * smoothstep(0.7, 0.0, d))
    canvas.shape((0, 0, 24, 48), drop)
    return canvas


def lance_strike():
    """A burst, white: a bright core, a ring and eight rays fading outwards; tinted by its element."""
    canvas = Canvas(96, 96)

    def burst(x, y):
        dx, dy = x - 48.0, y - 48.0
        r = math.sqrt(dx * dx + dy * dy) / 48.0
        if r >= 1.0:
            return None
        core = smoothstep(0.35, 0.0, r)
        ring = smoothstep(0.12, 0.0, abs(r - 0.62)) * 0.7
        line = min(abs(dx), abs(dy), abs(dx - dy) * 0.7071, abs(dx + dy) * 0.7071)
        rays = smoothstep(3.0, 0.0, line) * (1.0 - r) * 0.8
        return (255, 255, 255), min(1.0, core + ring + rays)
    canvas.shape((0, 0, 96, 96), burst)
    return canvas


def solid():
    """An opaque white square: the health bars tint and stretch it."""
    canvas = Canvas(8, 8)
    canvas.shape((0, 0, 8, 8), lambda x, y: ((255, 255, 255), 1.0))
    return canvas


# World-map cells: opaque 256-pixel squares inked at the border, for the zoomable grid (mipmapping).

def cell(seed):
    canvas = paper(256, 256, seed, wash=0.18)
    for i in range(256 * 256):
        x, y = i % 256, i // 256
        border = min(x, y, 255 - x, 255 - y)
        if border < 3:
            canvas.over(i, INK, 0.55 if border < 2 else 0.25)
    return canvas


def cell_plain():
    canvas = cell(21)
    grass(canvas, random.Random(21), 14, (20, 30, 230, 240), size=0.8)
    return canvas


def cell_mountain():
    canvas = cell(22)
    for peak, (cx, top, half) in enumerate([(70, 70, 70), (170, 40, 90), (120, 110, 60)]):
        def mountain(x, y, cx=cx, top=top, half=half, peak=peak):
            ridge = top + abs(x - cx) * (150.0 / half) * (0.8 + 0.4 * noise(x / 18.0, 0.5, 40 + peak))
            if y < ridge or y > 230:
                return None
            return mix(INK, (120, 128, 132), smoothstep(0.0, 90.0, y - ridge)), 0.85 * smoothstep(230.0, 150.0, y)
        canvas.shape((cx - 2 * half, top, cx + 2 * half, 230), mountain)
    return canvas


def cell_forest():
    canvas = cell(23)
    rng = random.Random(23)
    for _ in range(26):
        x, y = rng.uniform(26, 230), rng.uniform(50, 226)
        canvas.stroke([(x, y), (x, y - 26)], 3, 1.5, (72, 58, 48), 0.9)
        for k in range(4):
            canvas.dab(x + rng.uniform(-3, 3), y - 8 - k * 6, 9 - k * 1.8, PINE, 0.55, dry=0.3,
                       seed=rng.randint(0, 1 << 20))
    return canvas


def cell_city():
    canvas = cell(24)

    def wall(x, y):
        if not 36 <= x <= 220 or not 120 <= y <= 214:
            return None
        if y < 132 and int(x - 36) % 20 >= 12:
            return None  # battlements
        return mix((170, 160, 140), INK, 0.2 + 0.4 * (y - 120) / 94), 1.0
    canvas.shape((36, 120, 220, 214), wall)
    canvas.shape((112, 170, 144, 214), lambda x, y: ((50, 46, 48), 1.0))  # the gate
    for cx, top in ((80, 72), (128, 52), (178, 76)):
        canvas.shape((cx - 18, top + 16, cx + 18, 122), lambda x, y: ((196, 186, 164), 1.0))

        def roof(x, y, cx=cx, top=top):
            t = (x - cx) / 40.0
            if abs(t) > 1.0 or not top + 4 + 10 * t * t <= y <= top + 22 - 12 * t * t * t * t:
                return None  # a ridge over eaves that sweep up at the ends, like the shrine's
            return mix(VERMILION, INK, 0.3 + 0.3 * (y - top) / 22), 1.0
        canvas.shape((cx - 40, top, cx + 40, top + 24), roof)
    return canvas


# The seals' art: white shapes on a transparent ground, tinted by the seal's colours. The game
# computes each sprite's distance field from its alpha as it loads, so the art carries none: only a one-pixel antialiased
# edge. Drawn at 4x on a bit mask and reduced by averaging; arcs come from a rational half-angle parametrisation and
# Beziers, never a transcendental function.

SUPER = 4


class Mask:
    """A 4x supersampled bit mask, stamped with discs along paths; coordinates are in final pixels."""

    def __init__(self, width, height):
        self.width, self.height = width, height
        self.stride = width * SUPER
        self.bits = bytearray(self.stride * height * SUPER)

    def disc(self, cx, cy, radius):
        r = radius * SUPER
        px, py = cx * SUPER, cy * SUPER
        for y in range(max(0, math.ceil(py - r)), min(self.height * SUPER, math.floor(py + r) + 1)):
            dy = y + 0.5 - py
            span = r * r - dy * dy
            if span < 0.0:
                continue
            half = math.sqrt(span)
            x0, x1 = max(0, math.ceil(px - half - 0.5)), min(self.stride, math.floor(px + half - 0.5) + 1)
            if x1 > x0:
                self.bits[y * self.stride + x0:y * self.stride + x1] = b"\x01" * (x1 - x0)

    def path(self, points, width0, width1=None):
        """A round-capped polyline whose width tapers from width0 to width1 along its length."""
        width1 = width0 if width1 is None else width1
        lengths = [0.0]
        for (ax, ay), (bx, by) in itertools.pairwise(points):
            lengths.append(lengths[-1] + math.sqrt((bx - ax) ** 2 + (by - ay) ** 2))
        total = lengths[-1] or 1.0
        for (ax, ay), (bx, by), start, end in zip(points, points[1:], lengths, lengths[1:]):
            steps = max(1, math.ceil((end - start) * SUPER))
            for step in range(steps + 1):
                t = step / steps
                along = (start + (end - start) * t) / total
                self.disc(ax + (bx - ax) * t, ay + (by - ay) * t, (width0 + (width1 - width0) * along) * 0.5)

    def canvas(self):
        """Box-reduces to a white Canvas whose alpha is the covered fraction."""
        canvas = Canvas(self.width, self.height)
        area = float(SUPER * SUPER)
        for y in range(self.height):
            rows = [self.bits[(y * SUPER + j) * self.stride:(y * SUPER + j + 1) * self.stride] for j in range(SUPER)]
            columns = [sum(column) for column in zip(*rows)]
            for x in range(self.width):
                covered = sum(columns[x * SUPER:(x + 1) * SUPER])
                if covered:
                    i = y * self.width + x
                    canvas.r[i] = canvas.g[i] = canvas.b[i] = canvas.a[i] = covered / area
        return canvas


def bezier(*p, steps=48):
    """Points along a quadratic (three points) or cubic (four points) Bezier curve."""
    out = []
    for step in range(steps + 1):
        t = step / steps
        u = 1.0 - t
        if len(p) == 3:
            w = (u * u, 2 * u * t, t * t)
        else:
            w = (u ** 3, 3 * u * u * t, 3 * u * t * t, t ** 3)
        out.append((sum(a * q[0] for a, q in zip(w, p)), sum(a * q[1] for a, q in zip(w, p))))
    return out


def ellipse(cx, cy, rx, ry, per_quadrant=32):
    """A closed ellipse: four quadrants of the half-angle parametrisation x = (1-t^2)/(1+t^2), y = 2t/(1+t^2)."""
    quadrant = []
    for step in range(per_quadrant + 1):
        t = step / per_quadrant
        quadrant.append(((1.0 - t * t) / (1.0 + t * t), 2.0 * t / (1.0 + t * t)))
    points = list(quadrant)
    points += [(-y, x) for x, y in quadrant[1:]]
    points += [(-x, -y) for x, y in quadrant[1:]]
    points += [(y, -x) for x, y in quadrant[1:]]
    return [(cx + x * rx, cy + y * ry) for x, y in points]


def spiral(cx, cy, r0, r1, steps, step_t):
    """A spiral outwards from radius r0 to r1, turning a unit vector by the angle whose half-angle tangent is step_t."""
    c, s = (1.0 - step_t * step_t) / (1.0 + step_t * step_t), 2.0 * step_t / (1.0 + step_t * step_t)
    x, y = 1.0, 0.0
    out = []
    for i in range(steps + 1):
        r = r0 + (r1 - r0) * i / steps
        out.append((cx + x * r, cy + y * r))
        x, y = x * c - y * s, x * s + y * c
    return out


def lcg(seed):
    """The mockup's generator: a linear congruential stream of floats in [0, 1)."""
    state = seed & 0xFFFFFFFF
    while True:
        state = (state * 1664525 + 1013904223) & 0xFFFFFFFF
        yield state / 4294967296.0


RUNES = 24
# The grid keeps a stroke's round cap and its soft edge inside the canvas: 4 px from the sides, 2.5 from top and bottom
# at the widest stroke (8 px).
RUNE_GRID = [(24.0 + (x - 1) * 16.0, 32.0 + (y - 1.5) * 17.0) for y in range(4) for x in range(3)]


def rune_strokes():
    """The runes' strokes, one list of (a, b, width) each: 2 to 5 distinct segments between points of a 3 x 4 grid, 6 to
    8 pixels wide, and no two runes with the same segments (a set already used is drawn again from the same stream)."""
    rand = lcg(1000)
    used = set()
    runes = []
    while len(runes) < RUNES:
        strokes = []
        for _ in range(2 + int(next(rand) * 3)):
            a = RUNE_GRID[int(next(rand) * len(RUNE_GRID))]
            b = a
            while b == a:
                b = RUNE_GRID[int(next(rand) * len(RUNE_GRID))]
            width = 6.0 + int(next(rand) * 3)
            if all({a, b} != {c, d} for c, d, _ in strokes):
                strokes.append((a, b, width))
        key = frozenset(frozenset((a, b)) for a, b, _ in strokes)
        if len(strokes) >= 2 and key not in used:
            used.add(key)
            runes.append(strokes)
    return runes


def rune(index):
    """An invented rune (48 x 64): see rune_strokes."""
    mask = Mask(48, 64)
    for a, b, width in rune_strokes()[index]:
        mask.path([a, b], width)
    return mask.canvas()


# Each flame tongue: base, two control points and tip (256 x 128 canvas, y down), tapering from 14 to 4 pixels.
FLAMES = [
    [((128, 112), (84, 96), (176, 64), (150, 16))],
    [((128, 112), (60, 100), (100, 50), (64, 16)), ((128, 112), (196, 100), (156, 50), (192, 16))],
    [((128, 112), (40, 96), (72, 56), (40, 24)), ((128, 112), (128, 80), (100, 40), (128, 8)),
     ((128, 112), (216, 96), (184, 56), (216, 24))],
]


def flourish_flame(tongues):
    """A flame flourish (256 x 128): tapering curls rising from a base line."""
    mask = Mask(256, 128)
    mask.path(bezier((12, 110), (80, 118), (176, 118), (244, 110)), 10.0)
    for base, c1, c2, tip in FLAMES[tongues]:
        mask.path(bezier(base, c1, c2, tip), 14.0, 4.0)
        # A small curl peeling off each tongue's lower half, away from its tip.
        start = bezier(base, c1, c2, tip)[16]
        away = -1.0 if tip[0] > base[0] else 1.0
        mask.path(bezier(start, (start[0] + away * 30, start[1] - 4), (start[0] + away * 36, start[1] + 22)), 9.0, 4.0)
    return mask.canvas()


def emblem_lotus():
    """A lotus: five petals outlined from one base over a cupped line."""
    mask = Mask(256, 256)
    base = (128.0, 204.0)
    for tip in ((128, 36), (64, 64), (192, 64), (30, 120), (226, 120)):
        dx, dy = tip[0] - base[0], tip[1] - base[1]
        length = math.sqrt(dx * dx + dy * dy)
        nx, ny = -dy / length, dx / length
        mid = (base[0] + dx * 0.5, base[1] + dy * 0.5)
        for side in (-1.0, 1.0):
            mask.path(bezier(base, (mid[0] + side * nx * 70, mid[1] + side * ny * 70), tip), 12.0)
    mask.path(bezier((36, 200), (128, 256), (220, 200)), 14.0)
    return mask.canvas()


def emblem_swords():
    """Two crossed swords in a ring."""
    mask = Mask(256, 256)
    mask.path(ellipse(128, 128, 114, 114), 12.0)
    for sign in (-1.0, 1.0):
        pommel, tip = (128 - 76 * sign, 204), (128 + 76 * sign, 52)
        mask.path([pommel, tip], 20.0, 8.0)
        # The cross-guard sits a third of the way from the pommel, across the blade.
        gx, gy = pommel[0] + (tip[0] - pommel[0]) / 3, pommel[1] + (tip[1] - pommel[1]) / 3
        mask.path([(gx - 24, gy - 24 * sign), (gx + 24, gy + 24 * sign)], 14.0)
        mask.disc(pommel[0], pommel[1], 13.0)
    return mask.canvas()


def emblem_eye():
    """An open eye: almond lids, a ringed iris, a pupil and three lashes."""
    mask = Mask(256, 256)
    mask.path(bezier((16, 136), (80, 48), (176, 48), (240, 136)), 14.0)
    mask.path(bezier((16, 136), (80, 224), (176, 224), (240, 136)), 14.0)
    mask.path(ellipse(128, 136, 40, 40), 12.0)
    mask.disc(128, 136, 16.0)
    for x0, y0, x1, y1 in ((128, 84, 128, 36), (84, 92, 62, 52), (172, 92, 194, 52)):
        mask.path([(x0, y0), (x1, y1)], 12.0, 7.0)
    return mask.canvas()


def emblem_spiral():
    """A spiral of about three turns."""
    mask = Mask(256, 256)
    mask.path(spiral(128, 128, 10, 112, 150, 0.0625), 14.0)
    mask.disc(128, 128, 13.0)
    return mask.canvas()


# The map's overlays: transparent but for the drawing, a tile's 256 px square. A road tile has one of 16 masks, the bits
# of the neighbours carrying a road (N 1, E 2, S 4, W 8); each arm runs from the tile's centre to the middle of its edge,
# straight over the last stretch and painted by the same solid strokes at every tile, so neighbours join. Mask 0 is an
# isolated dot.

TILE = 256
MID = TILE / 2.0
OCHRE = (150, 110, 64)
PLANK = (150, 112, 70)


def curve(points, steps=12):
    """A Catmull-Rom curve through the points, ends included, arriving along its first and last segments."""
    padded = [points[0], *points, points[-1]]
    out = [points[0]]
    for p0, p1, p2, p3 in zip(padded, padded[1:], padded[2:], padded[3:]):
        for k in range(1, steps + 1):
            t = k / steps
            t2, t3 = t * t, t * t * t
            out.append(tuple(
                0.5 * (2 * b + (c - a) * t + (2 * a - 5 * b + 4 * c - d) * t2 + (3 * b - a - 3 * c + d) * t3)
                for a, b, c, d in zip(p0, p1, p2, p3)))
    return out


def arm(seed, layers):
    """The east arm: from just outside the east edge's middle to the centre, straight over its last stretch."""
    rng = random.Random(seed)
    off = rng.uniform(-6.0, 6.0)
    points = curve([(TILE + 8.0, MID), (MID + 88, MID), (MID + 48, MID + off), (MID + 16, MID), (MID, MID)])
    canvas = Canvas(TILE, TILE)
    for width, colour, alpha, dry, sd in layers:
        canvas.stroke(points, width, width, colour, alpha, dry, sd)
    return canvas


def remap(canvas, source):
    """A canvas whose pixel (x, y) is the original's pixel source(x, y); the maps used here are exact."""
    out = Canvas(TILE, TILE)
    for y in range(TILE):
        for x in range(TILE):
            sx, sy = source(x, y)
            i, j = y * TILE + x, sy * TILE + sx
            out.r[i], out.g[i], out.b[i], out.a[i] = canvas.r[j], canvas.g[j], canvas.b[j], canvas.a[j]
    return out


def lay(canvas, top):
    for i in range(TILE * TILE):
        keep = 1.0 - top.a[i]
        canvas.r[i] = top.r[i] + canvas.r[i] * keep
        canvas.g[i] = top.g[i] + canvas.g[i] * keep
        canvas.b[i] = top.b[i] + canvas.b[i] * keep
        canvas.a[i] = top.a[i] + canvas.a[i] * keep


def line_tile(mask, seed, layers, dot):
    """The mask's arms laid over one another. The west arm mirrors the east one and the north the south, which is
    the east arm turned about the diagonal, so the two tiles meeting at an edge paint the same pixels along it."""
    canvas = Canvas(TILE, TILE)
    if not mask:
        for width, colour, alpha, dry, sd in layers:
            canvas.dab(MID, MID, width * dot, colour, alpha, dry=dry, seed=sd)
        return canvas
    east, south = arm(seed, layers), remap(arm(seed + 1, layers), lambda x, y: (y, x))
    drawn = {1: remap(south, lambda x, y: (x, TILE - 1 - y)), 2: east, 4: south,
             8: remap(east, lambda x, y: (TILE - 1 - x, y))}
    for bit, picture in drawn.items():
        if mask & bit:
            lay(canvas, picture)
    return canvas


def road(mask):
    """A worn track: a dark soft verge, an ochre bed and dry light wheel marks."""
    return line_tile(mask, 60, [(19, INK, 0.28, 0.35, 1), (14, OCHRE, 1.0, 0.0, 2), (8, (190, 152, 98), 0.55, 0.5, 3),
                                (3, (104, 74, 44), 0.45, 0.6, 4)], 0.5)


def plank_bridge(across):
    """A plank deck spanning the river's half-tile band, with ink rails; the road runs along `across` ('h':
    east-west, 'v': north-south)."""
    canvas = Canvas(TILE, TILE)

    def at(a, b):
        return (MID + a, MID + b) if across == "h" else (MID + b, MID + a)
    for a in range(-64, 65, 8):
        canvas.stroke([at(a, -21), at(a, 21)], 6.5, 6.5, PLANK, 1.0, 0.1, 40 + a)
    canvas.stroke([at(-70, -24), at(70, -24)], 4.0, 4.0, INK, 0.85, 0.15, 5)
    canvas.stroke([at(-70, 24), at(70, 24)], 4.0, 4.0, INK, 0.85, 0.15, 6)
    for a in (-70, 70):
        for b in (-24, 24):
            x, y = at(a, b)
            canvas.dab(x, y, 5.5, INK, 0.9, dry=0.1, seed=a + b)
    return canvas


def ford():
    """Pale shallows with stepping stones spanning the river's half-tile band; it reads the same across either way."""
    canvas = Canvas(TILE, TILE)
    canvas.dab(MID, MID, 70, (188, 214, 196), 0.55, soft=1.0, dry=0.3, seed=7)
    rng = random.Random(62)
    for i in range(10):
        x, y = MID + (i - 4.5) * 13.0 + rng.uniform(-3, 3), MID + (i % 2 - 0.5) * 20.0 + rng.uniform(-3, 3)
        r = rng.uniform(6.0, 8.5)
        canvas.dab(x + 2, y + 3, r, INK, 0.35, dry=0.0, seed=i)
        canvas.dab(x, y, r, (160, 156, 148), 1.0, dry=0.2, seed=i + 9)
        canvas.dab(x - r * 0.3, y - r * 0.3, r * 0.45, (212, 208, 196), 0.7, dry=0.3, seed=i + 19)
    return canvas


# Place markers: bold ink glyphs on a paper disc, about 96 px, the foot at the centre.

MARKER = 96
CENTRE = MARKER / 2.0


def inside(polygon, x, y):
    hit = False
    for (ax, ay), (bx, by) in zip(polygon, polygon[1:] + polygon[:1]):
        if (ay > y) != (by > y) and x < ax + (y - ay) * (bx - ax) / (by - ay):
            hit = not hit
    return hit


def fill(canvas, polygon, colour, alpha=1.0):
    box = (min(p[0] for p in polygon), min(p[1] for p in polygon), max(p[0] for p in polygon),
           max(p[1] for p in polygon))
    canvas.shape(box, lambda x, y: (colour, alpha) if inside(polygon, x, y) else None)


def ink_line(canvas, points, width=4.5, colour=INK, alpha=0.95):
    canvas.stroke(points, width, width, colour, alpha, 0.1, int(points[0][0] * 7 + points[0][1]))


def marker_disc():
    canvas = Canvas(MARKER, MARKER)

    def coverage(x, y):
        d = math.sqrt((x - CENTRE) ** 2 + (y - CENTRE) ** 2)
        if d >= 45.0:
            return None
        cover = min(1.0, (45.0 - d) * 1.5)
        if d > 40.0:
            return INK, cover * 0.9
        shade = smoothstep(18.0, 40.0, d) * 0.18
        return mix(PAPER, (196, 182, 150), shade), cover
    canvas.shape((0, 0, MARKER, MARKER), coverage)
    return canvas


def glyph_city(canvas):
    fill(canvas, [(26, 68), (26, 46), (32, 46), (32, 41), (38, 41), (38, 46), (44, 46), (44, 41), (52, 41), (52, 46),
                  (58, 46), (58, 41), (64, 41), (64, 46), (70, 46), (70, 68)], INK, 0.9)
    fill(canvas, [(40, 41), (48, 22), (56, 41)], VERMILION, 0.95)
    fill(canvas, [(43, 68), (43, 56), (48, 51), (53, 56), (53, 68)], PAPER, 1.0)


def glyph_haunted_forest(canvas):
    for x, y, s in ((34, 66, 1.0), (60, 68, 1.15)):
        fill(canvas, [(x, y - 40 * s), (x - 15 * s, y - 14 * s), (x + 15 * s, y - 14 * s)], INK, 0.9)
        fill(canvas, [(x, y - 26 * s), (x - 19 * s, y), (x + 19 * s, y)], INK, 0.9)
        ink_line(canvas, [(x, y), (x, y + 6 * s)], 4.0)
    canvas.dab(62, 46, 3.6, VERMILION, 1.0)
    canvas.dab(72, 46, 3.6, VERMILION, 1.0)


def glyph_mountain_pass(canvas):
    fill(canvas, [(20, 70), (36, 30), (46, 52), (38, 70)], INK, 0.9)
    fill(canvas, [(58, 70), (66, 56), (76, 24), (88, 70)], INK, 0.9)
    fill(canvas, [(36, 30), (40, 38), (31, 38)], PAPER, 0.95)
    fill(canvas, [(76, 24), (82, 36), (70, 36)], PAPER, 0.95)
    ink_line(canvas, [(48, 70), (50, 62), (56, 56), (52, 50)], 3.0, VERMILION, 0.95)


def glyph_shrine(canvas):
    ink_line(canvas, [(32, 70), (32, 36)], 6.0)
    ink_line(canvas, [(64, 70), (64, 36)], 6.0)
    ink_line(canvas, [(26, 38), (70, 38)], 6.5, VERMILION)
    ink_line(canvas, [(22, 31), (74, 31)], 6.0, VERMILION)
    ink_line(canvas, [(32, 50), (64, 50)], 4.0)


def glyph_cave(canvas):
    fill(canvas, [(18, 70), (24, 50), (36, 36), (48, 30), (60, 36), (72, 50), (78, 70)], (112, 108, 100), 0.95)
    mouth = [(34, 70), (36, 56), (42, 48), (48, 46), (54, 48), (60, 56), (62, 70)]
    fill(canvas, mouth, INK, 0.97)
    ink_line(canvas, [(18, 70), (24, 50), (36, 36), (48, 30), (60, 36), (72, 50), (78, 70)], 3.5)


def glyph_cliff(canvas):
    fill(canvas, [(20, 30), (46, 30), (50, 36), (60, 34), (76, 38), (76, 70), (20, 70)], (126, 120, 108), 0.95)
    ink_line(canvas, [(20, 30), (46, 30), (50, 36), (60, 34), (76, 38)], 4.5)
    for x in (30, 42, 54, 66):
        ink_line(canvas, [(x, 42), (x - 3, 56), (x + 1, 68)], 2.6, INK, 0.7)
    fill(canvas, [(20, 70), (76, 70), (80, 76), (16, 76)], INK, 0.6)


def glyph_corrupt_village(canvas):
    for x, y, s in ((34, 66, 0.85), (60, 70, 1.1)):
        fill(canvas, [(x - 14 * s, y), (x - 14 * s, y - 16 * s), (x, y - 30 * s), (x + 14 * s, y - 16 * s),
                      (x + 14 * s, y)], INK, 0.9)
        fill(canvas, [(x - 3 * s, y), (x - 3 * s, y - 10 * s), (x + 4 * s, y - 10 * s), (x + 4 * s, y)], PAPER, 0.9)
    ink_line(canvas, [(26, 26), (46, 46)], 4.5, VERMILION)
    ink_line(canvas, [(46, 26), (26, 46)], 4.5, VERMILION)


GLYPHS = {"city": glyph_city, "haunted_forest": glyph_haunted_forest, "mountain_pass": glyph_mountain_pass,
          "shrine": glyph_shrine, "cave": glyph_cave, "cliff": glyph_cliff, "corrupt_village": glyph_corrupt_village}


def marker(sort):
    canvas = marker_disc()
    GLYPHS[sort](canvas)
    return canvas


def route_dot():
    canvas = Canvas(16, 16)

    def coverage(x, y):
        d = math.sqrt((x - 8.0) ** 2 + (y - 8.0) ** 2)
        if d >= 7.5:
            return None
        return (INK, min(1.0, (7.5 - d) * 0.9)) if d > 5.0 else (VERMILION, 1.0)
    canvas.shape((0, 0, 16, 16), coverage)
    return canvas


def route_marker():
    canvas = Canvas(48, 48)

    def coverage(x, y):
        d = math.sqrt((x - 24.0) ** 2 + (y - 24.0) ** 2)
        if 17.0 <= d <= 22.0:
            return VERMILION, min(1.0, (d - 16.5) * 0.5, (22.5 - d) * 0.5) * 0.95
        return None
    canvas.shape((0, 0, 48, 48), coverage)
    return canvas


def target_tile():
    canvas = Canvas(256, 256)

    def coverage(x, y):
        edge = min(x, y, 256.0 - x, 256.0 - y)
        if edge < 8.0:
            return VERMILION, 0.85
        return None
    canvas.shape((0, 0, 256, 256), coverage)
    return canvas


CLOUDS = 16
CLOUD_BODY = (240, 235, 222)
CLOUD_INK = (120, 104, 90)


def cloud_draw(variant, index):
    """The variant's `index`th draw in [0, 1): integer hashing only, so a cloud is the same on every host."""
    return hash01(variant, 17, index)


def cloud_parts(x, y, wid, variant):
    """The lobes of a curl cloud: a base of two and two to four bumps on top, as (x, y, radius)."""
    half = wid / 2.0
    draw = lambda index: cloud_draw(variant, index)
    parts = [(x - (0.35 + 0.2 * draw(1)) * half, y + 0.05 * half, (0.36 + 0.12 * draw(2)) * half),
             (x + (0.35 + 0.2 * draw(3)) * half, y + 0.05 * half, (0.36 + 0.12 * draw(4)) * half)]
    for bump in range(2 + int(draw(5) * 3)):
        parts.append((x + (draw(10 + bump) - 0.5) * 0.9 * half, y - (0.1 + 0.3 * draw(20 + bump)) * half,
                      (0.3 + 0.25 * draw(30 + bump)) * half))
    return parts


def cloud(variant):
    """A 祥云 curl cloud: cream body, ink outline, one or two ink spirals; 256 by 176 with its foot at the centre.

    The lobes' centre sits below the foot so the tallest bump and the lowest lobe both stay on the sprite.
    """
    canvas = Canvas(256, 176)
    wid, cx, cy = 228.0, 128.0, 109.0
    line = wid * 0.022
    parts = cloud_parts(cx, cy, wid, variant)

    def coverage(x, y):
        edge = min(math.sqrt((x - px) ** 2 + (y - py) ** 2) - r for px, py, r in parts)
        outer = min(1.0, max(0.0, line + 0.5 - edge))
        if outer <= 0.0:
            return None
        inner = min(1.0, max(0.0, 0.5 - edge))
        return mix(CLOUD_INK, CLOUD_BODY, inner), outer
    canvas.shape((0, 0, 255, 175), coverage)
    for n in range(1 if cloud_draw(variant, 40) < 0.75 else 2):
        sx = cx + (cloud_draw(variant, 41 + n) - 0.5) * 0.45 * wid
        sy = cy + (cloud_draw(variant, 43 + n) - 0.6) * 0.18 * wid
        radius = wid * (0.09 + 0.09 * cloud_draw(variant, 45 + n))
        turns = 1.0 + 1.2 * cloud_draw(variant, 47 + n)
        steps = 60
        # 60 steps turn by a small angle each, so tan(angle / 2) is angle / 2 closely enough and the chords stay smooth
        step_t = (math.pi * turns / steps) * (-1.0 if cloud_draw(variant, 49 + n) < 0.5 else 1.0)
        points = [(x, sy + (y - sy) * 0.8) for x, y in spiral(sx, sy, radius * 0.15, radius, steps, step_t)]
        canvas.stroke(points[::-1], line, line, CLOUD_INK, 1.0)
    return canvas


# The painted terrain's marks: sparse stamps, mist and places over a paper ground, each on a transparent sheet.

WASH_INK = (40, 38, 36)


def blob(canvas, cx, cy, rx, ry, colour, alpha, rim=WASH_INK, rim_alpha=0.6, rim_width=2.0, clip=None):
    """A soft-edged ellipse with an ink rim; `clip` cuts it off below that y, leaving a dome."""
    def coverage(x, y):
        if clip is not None and y > clip:
            return None
        d = math.sqrt(((x - cx) / rx) ** 2 + ((y - cy) / ry) ** 2)
        edge = (1.0 - d) * min(rx, ry)
        outer = min(1.0, max(0.0, edge + 0.5))
        if outer <= 0.0:
            return None
        if rim is None:
            return colour, alpha * outer
        t = min(1.0, max(0.0, edge - rim_width + 0.5))
        return mix(rim, colour, t), (rim_alpha + (alpha - rim_alpha) * t) * outer
    canvas.shape((cx - rx - 1, cy - ry - 1, cx + rx + 1, cy + ry + 1), coverage)


def poly(canvas, points, colour, alpha=1.0, rim=WASH_INK, rim_alpha=0.7, width=2.0):
    fill(canvas, points, colour, alpha)
    if rim:
        canvas.stroke(points + points[:1], width, width, rim, rim_alpha, 0.1, int(points[0][0] * 3 + points[0][1]))


def ink(canvas, points, width=2.0, colour=WASH_INK, alpha=0.75):
    canvas.stroke(points, width, width, colour, alpha, 0.1, int(points[0][0] * 5 + points[0][1]))


def stamp_tree(canvas, x, y, s, rng):
    ink(canvas, [(x, y), (x + rng.uniform(-1, 1) * s * 0.06, y - s * 0.55)], max(2.0, s * 0.07), alpha=0.85)
    tint = (66, 108, 76)
    for i in range(3):
        cx, cy, r = x + rng.uniform(-0.2, 0.2) * s, y - s * (0.45 + i * 0.18), s * (0.34 - i * 0.07)
        shade = 0.82 + 0.1 * i
        blob(canvas, cx, cy, r, r * 0.72, tuple(c * shade for c in tint), 0.92, rim_alpha=0.6,
             rim_width=max(1.5, s * 0.03))


def stamp_pine(canvas, x, y, s):
    ink(canvas, [(x, y), (x, y - s * 0.9)], max(2.0, s * 0.06), alpha=0.85)
    for i in range(4):
        yy, w = y - s * (0.3 + i * 0.17), s * (0.36 - i * 0.07)
        poly(canvas, [(x - w, yy), (x + w, yy), (x, yy - s * 0.24)], (50, 88, 72), 0.93, rim_alpha=0.6,
             width=max(1.5, s * 0.03))


def stamp_mountain(canvas, x, y, w, h, body, ridge, snowcap=False):
    pts = [(x - w / 2, y), (x - w * 0.12, y - h * 0.8), (x, y - h), (x + w * 0.18, y - h * 0.7), (x + w / 2, y)]
    poly(canvas, pts, body, 0.95, rim=ridge, rim_alpha=0.85, width=max(2.0, w * 0.03))
    poly(canvas, [(x, y - h), (x + w * 0.18, y - h * 0.7), (x + w / 2, y), (x + w * 0.05, y)],
         tuple(c * 0.8 for c in body), 0.35, rim=None)
    ink(canvas, [(x, y - h), (x - w * 0.05, y - h * 0.5), (x + w * 0.04, y - h * 0.2)], max(1.5, w * 0.02), ridge, 0.55)
    if snowcap:
        poly(canvas, [(x - w * 0.12, y - h * 0.8), (x, y - h), (x + w * 0.14, y - h * 0.76), (x, y - h * 0.7)],
             (250, 250, 252), 0.98, rim=ridge, rim_alpha=0.5, width=max(1.5, w * 0.02))


def stamp_hill(canvas, x, y, w, h):
    blob(canvas, x, y, w / 2, h, (180, 154, 116), 0.88, rim_alpha=0.75, rim_width=max(2.0, w * 0.03), clip=y)


def stamp_rocks(canvas, x, y, s, rng):
    for _ in range(3):
        cx, cy, r = x + rng.uniform(-0.38, 0.38) * s, y - rng.uniform(0.0, 0.3) * s * 0.5, s * rng.uniform(0.12, 0.2)
        poly(canvas, [(cx - r, cy), (cx - r * 0.6, cy - r * 1.2), (cx + r * 0.3, cy - r * 1.4), (cx + r, cy)],
             (116, 112, 108), 0.95, rim_alpha=0.85, width=max(2.0, r * 0.14))


def stamp_reeds(canvas, x, y, s, rng):
    blob(canvas, x, y - s * 0.03, s * 0.35, s * 0.07, (124, 156, 154), 0.55, rim=None)
    for _ in range(9):
        xx = x + rng.uniform(-0.38, 0.38) * s
        ink(canvas, [(xx, y), (xx + rng.uniform(-0.06, 0.06) * s, y - s * rng.uniform(0.25, 0.5))],
            max(2.0, s * 0.03), (74, 84, 52), 0.9)


def stamp_dunes(canvas, x, y, s, rng):
    for i in range(2):
        base = y - i * s * 0.2
        dx = rng.uniform(-0.12, 0.12) * s
        blob(canvas, x + dx, base, s * 0.36, s * 0.16, (226, 204, 156), 0.7, rim=(150, 118, 70), rim_alpha=0.75,
             rim_width=max(2.0, s * 0.02), clip=base)


def stamp_tufts(canvas, x, y, s, rng):
    for _ in range(4):
        xx, yy = x + rng.uniform(-0.38, 0.38) * s, y - rng.uniform(0.0, 0.2) * s
        for k in (-1, 0, 1):
            ink(canvas, [(xx, yy), (xx + k * s * 0.06, yy - s * rng.uniform(0.12, 0.2))], max(2.0, s * 0.03),
                (80, 114, 60), 0.9)


def stamp_flowers(canvas, x, y, s, rng):
    for _ in range(11):
        xx, yy = x + rng.uniform(-0.42, 0.42) * s, y - rng.uniform(0.12, 0.5) * s * 0.5
        ink(canvas, [(xx, yy + s * 0.08), (xx, yy)], max(2.0, s * 0.02), (90, 120, 66), 0.85)
        petal = (214, 120, 120) if rng.random() < 0.6 else (232, 190, 96)
        blob(canvas, xx, yy, s * 0.05, s * 0.05, petal, 0.95, rim=WASH_INK, rim_alpha=0.45, rim_width=1.5)
        blob(canvas, xx, yy, s * 0.02, s * 0.02, (250, 238, 190), 1.0, rim=None)


def stamp_ripples(canvas, x, y, s, rng):
    for i in range(3):
        xx, yy = x + rng.uniform(-0.3, 0.3) * s, y - s * 0.1 - i * s * 0.12
        arc = bezier((xx - s * 0.2, yy), (xx, yy - s * 0.1), (xx + s * 0.2, yy), steps=16)
        ink(canvas, [(px, py + 2) for px, py in arc], max(2.0, s * 0.025), (70, 104, 118), 0.35)
        ink(canvas, arc, max(2.0, s * 0.025), (232, 242, 246), 0.9)


# Room in texels a stamp leaves clear at its sides and top: at the far zoom its art is drawn from small mips, blurred about this
# far out, and the sprite's quad cutting into that blur would leave a hard edge that crawls as the camera moves.
STAMP_MARGIN = 16
# kind -> (width, height): each as wide as 256 times its largest size on the map.
STAMPS = {"forest": (333, 230), "hills": (307, 130), "peaks": (435, 300), "snow": (435, 260), "rock": (282, 120),
          "marsh": (230, 100), "sand": (230, 90), "grass": (230, 90), "meadow": (230, 90), "water": (230, 100)}


# How a forest clump is fitted inside STAMP_MARGIN. Its trees shrink by the first scale that keeps the tallest under the
# top's margin; then, if it still reaches into a side's, (spread, scale) are tried in turn: its spacing closes first, as
# overlapping crowns still read as one clump, and only then do its trees shrink further.
FOREST_SCALES = (1.0, 0.95, 0.9, 0.85, 0.8, 0.75, 0.7, 0.65, 0.6, 0.55, 0.5)
FOREST_SPREADS = (1.0, 0.9, 0.8, 0.7, 0.6, 0.5)


def forest_clump(canvas, x, y, bottom, spacing, height, count, seed, spread, scale):
    """`count` trees, broad ones and pines, side by side about `x`, their feet up to 0.18 `height` above `y` and their
    trunks' round ends a texel above `bottom`: `spacing` apart times `spread`, sized from `height` times `scale`, every
    random draw from `seed`."""
    rng = random.Random(seed)
    for i in range(count):
        fx = x + ((i - (count - 1) / 2.0) * spacing + rng.uniform(-8, 8)) * spread
        fy = y - rng.uniform(0.0, 0.18) * height
        size = height * rng.uniform(0.62, 0.8) * scale
        fy = min(fy, bottom - 1.0 - size * 0.035)
        if rng.random() < 0.4:
            stamp_pine(canvas, fx, fy, size)
        else:
            stamp_tree(canvas, fx, fy, size, rng)


def forest(width, height, variant, seed):
    """A forest stamp: 3 to 5 trees as large and as wide apart as STAMP_MARGIN allows (FOREST_SCALES, FOREST_SPREADS),
    measured on a sheet large enough that nothing they draw is cut."""
    count, pad = 3 + variant % 3, width // 2

    def drawn(spread, scale):
        trial = Canvas(width + 2 * pad, height + 2 * pad)
        forest_clump(trial, pad + width / 2.0, pad + height - 6.0, pad + height, width * 0.22, height, count, seed, spread,
                     scale)
        alpha = [[trial.a[row * trial.width + column] * 255.0 + 0.5 >= 1.0 for column in range(trial.width)]
                 for row in range(trial.height)]
        columns = [column for column in range(trial.width) if any(row[column] for row in alpha)]
        top = next(row for row in range(trial.height) if any(alpha[row]))
        return trial, columns[0] - pad, pad + width - 1 - columns[-1], top - pad

    tallest = next((scale for scale in FOREST_SCALES if drawn(1.0, scale)[3] >= STAMP_MARGIN), None)
    if tallest is None:
        raise ValueError(f"no scale keeps forest variant {variant} clear of its top margin")
    fits = [(spread, tallest) for spread in FOREST_SPREADS] + \
           [(FOREST_SPREADS[-1], scale) for scale in FOREST_SCALES if scale < tallest]
    for spread, scale in fits:
        trial, left, right, _ = drawn(spread, scale)
        if min(left, right) >= STAMP_MARGIN:
            break
    else:
        raise ValueError(f"no fit keeps forest variant {variant} inside its margin")
    canvas = Canvas(width, height)
    for row in range(height):
        start, inside = (row + pad) * trial.width + pad, row * width
        for channel, out in ((trial.r, canvas.r), (trial.g, canvas.g), (trial.b, canvas.b), (trial.a, canvas.a)):
            out[inside:inside + width] = channel[start:start + width]
    return canvas


def stamp(kind, variant):
    """Marks of one terrain kind, drawn in an area as wide as the kind's largest stamp, the foot at the bottom centre."""
    width, height = STAMPS[kind]
    seed = 900 + 17 * variant + sum(map(ord, kind))
    if kind == "forest":
        return forest(width, height, variant, seed)
    canvas = Canvas(width, height)
    rng = random.Random(seed)
    x, y = width / 2.0, height - 6.0
    if kind == "peaks":
        # Their summits STAMP_MARGIN below the top, too: the cut would crawl there as at the sides.
        stamp_mountain(canvas, x - width * 0.12, y, width * 0.52, height * 0.9, (96, 140, 130), (34, 60, 58),
                       variant % 2 == 0)
        stamp_mountain(canvas, x + width * 0.2, y - 2, width * 0.42, height * 0.62, (120, 158, 140), (40, 66, 60))
        if variant >= 2:
            third = width * 0.2
            # Its foot's half-width and its rim (stamp_mountain()) inside the margin.
            reach = third / 2.0 + max(2.0, third * 0.03)
            stamp_mountain(canvas, width - STAMP_MARGIN - reach, y, third, height * 0.35, (108, 148, 134), (40, 66, 60),
                           True)
    elif kind == "snow":
        stamp_mountain(canvas, x + rng.uniform(-20, 20), y, width * 0.62, height * 0.88, (214, 222, 230),
                       (90, 100, 112), True)
    elif kind == "rock":
        stamp_rocks(canvas, x, y, width * 0.8, rng)
    elif kind == "hills":
        stamp_hill(canvas, x - width * 0.16, y, width * 0.52, height * 0.8)
        stamp_hill(canvas, x + width * 0.22, y - 2, width * 0.4, height * 0.58)
    elif kind == "marsh":
        stamp_reeds(canvas, x, y, height * 1.1, rng)
    elif kind == "sand":
        stamp_dunes(canvas, x, y, width * 0.7, rng)
    elif kind == "grass":
        stamp_tufts(canvas, x, y, width * 0.8, rng)
    elif kind == "meadow":
        stamp_flowers(canvas, x, y, width * 0.8, rng)
    elif kind == "water":
        stamp_ripples(canvas, x, y, width * 0.8, rng)
    return canvas


# Each mist is a few overlapping puffs as (centre x, centre y, half width, half height) in a 384 by 128 sheet; one
# puff always sits at the centre, so the alpha there is 1.
MISTS = [
    [(192, 64, 190, 62)],
    [(192, 64, 130, 60), (92, 72, 90, 48), (292, 58, 90, 50)],
    [(192, 64, 110, 62), (192, 74, 190, 32)],
    [(192, 64, 70, 60), (130, 76, 126, 50), (280, 52, 100, 46)],
]


def mist(variant):
    """Soft white puffs, 384 by 128, opaque at the centre and clear at the rim; the look tints them."""
    canvas = Canvas(384, 128)
    puffs = MISTS[variant]

    def coverage(x, y):
        alpha = 0.0
        for px, py, rx, ry in puffs:
            d = math.sqrt(((x - px) / rx) ** 2 + ((y - py) / ry) ** 2)
            alpha = max(alpha, 1.0 - d)
        if alpha <= 0.0:
            return None
        wobble = 1.0 + 0.2 * (noise(x / 50.0, y / 30.0, 300 + variant) - 0.5)
        return (255, 255, 255), min(1.0, alpha * wobble) ** 0.8
    canvas.shape((0, 0, 383, 127), coverage)
    return canvas


# Texels a painting's ground wash leaves clear under it: the wash is as wide as the sheet, and the sprite's quad cutting it
# at the bottom would leave a long hard edge that crawls as the camera moves.
WASH_ROOM = 4


def ground_wash(canvas, cx, cy, rx, ry):
    """The ground under a painting, an ellipse centred at (`cx`, `cy`), raised as far as it takes to end WASH_ROOM
    texels above the sheet's bottom."""
    blob(canvas, cx, min(cy, canvas.height - WASH_ROOM - ry), rx, ry, (196, 184, 150), 0.45, rim=None)


def roof(canvas, x, y, w, h, colour=(70, 68, 78), lw=2.5):
    """An upturned eave roof over the span x - w/2 .. x + w/2, its ridge h above y."""
    poly(canvas, [(x - w * 0.62, y + h * 0.15), (x - w * 0.5, y), (x - w * 0.12, y - h * 0.8), (x, y - h),
                  (x + w * 0.12, y - h * 0.8), (x + w * 0.5, y), (x + w * 0.62, y + h * 0.15), (x, y + h * 0.05)],
         colour, 0.97, rim_alpha=0.85, width=lw)


def house(canvas, x, y, w, h, k, colour=(214, 204, 182), roof_colour=(70, 68, 78), opacity=0.97):
    poly(canvas, [(x - w / 2, y), (x - w / 2, y - h), (x + w / 2, y - h), (x + w / 2, y)], colour, opacity, width=2.5 * k)
    roof(canvas, x, y - h, w * 1.05, h * 0.8, roof_colour, 2.5 * k)


def painting_city(size, tiers):
    """A walled city in ink and wash that fills its sheet: wall with battlements, corner towers, a gate, a keep and
    roofs behind; `tiers` rooftops and a stacked keep roof make the capital."""
    k = size / 512.0
    canvas = Canvas(size, size)
    cx, base = size / 2.0, size - 8.0 * k
    ground_wash(canvas, cx, base - 4 * k, 240 * k, 28 * k)
    top = base - 190 * k
    if tiers:
        for i in range(tiers):
            house(canvas, cx + (i - (tiers - 1) / 2.0) * 84 * k, top - 8 * k - i % 2 * 30 * k, 70 * k, 80 * k, k)
        # Opaque, so the rooftops behind the keep don't show through it.
        house(canvas, cx, top - 8 * k, 140 * k, 150 * k, k, (206, 192, 168), VERMILION, 1.0)
        roof(canvas, cx, top - 208 * k, 100 * k, 70 * k, VERMILION, 2.5 * k)
    else:
        for dx, w, h in ((-150, 110, 100), (150, 110, 90)):
            house(canvas, cx + dx * k, top - 8 * k, w * k, h * k, k)
        house(canvas, cx, top - 8 * k, 150 * k, 150 * k, k, (206, 192, 168), VERMILION)
    poly(canvas, [(cx - 220 * k, base), (cx - 220 * k, top), (cx + 220 * k, top), (cx + 220 * k, base)],
         (200, 190, 170), 0.98, width=3 * k)
    for i in range(-10, 10):
        x = cx + (i + 0.5) * 22 * k
        poly(canvas, [(x - 9 * k, top), (x - 9 * k, top - 14 * k), (x + 9 * k, top - 14 * k), (x + 9 * k, top)],
             (200, 190, 170), 0.98, width=2 * k)
    for dx in (-190, 190):
        tx = cx + dx * k
        poly(canvas, [(tx - 40 * k, base), (tx - 40 * k, top - 70 * k), (tx + 40 * k, top - 70 * k),
                      (tx + 40 * k, base)], (190, 180, 160), 0.98, width=3 * k)
        roof(canvas, tx, top - 70 * k, 90 * k, 70 * k, (70, 68, 78), 3 * k)
    for x in range(-200, 201, 40):
        ink(canvas, [(cx + x * k, top + 12 * k), (cx + x * k, base - 6 * k)], 1.5 * k, WASH_INK, 0.2)
    gate = [(cx - 30 * k, base), (cx - 30 * k, top + 70 * k), (cx - 18 * k, top + 42 * k), (cx, top + 30 * k),
            (cx + 18 * k, top + 42 * k), (cx + 30 * k, top + 70 * k), (cx + 30 * k, base)]
    poly(canvas, gate, (40, 36, 36), 0.97, width=3 * k)
    roof(canvas, cx, top + 18 * k, 100 * k, 40 * k, VERMILION, 3 * k)
    return canvas


def painting_capital():
    return painting_city(768, 5)


def painting_haunted_forest():
    canvas = Canvas(256, 256)
    ground_wash(canvas, 128, 244, 110, 14)
    blob(canvas, 128, 120, 60, 80, (210, 214, 222), 0.35, rim=None)
    for x, y, s in ((70, 240, 1.0), (128, 246, 1.25), (188, 238, 0.9)):
        ink(canvas, [(x, y), (x + 4 * s, y - 70 * s), (x - 6 * s, y - 130 * s)], 9 * s, WASH_INK, 0.9)
        for dx, dy, ex, ey in ((4, -70, 38, -112), (4, -70, -30, -100), (-6, -130, 18, -170), (-6, -130, -26, -160)):
            ink(canvas, [(x + dx * s, y + dy * s), (x + ex * s, y + ey * s)], 4.5 * s, WASH_INK, 0.85)
    for x, y in ((118, 150), (140, 150)):
        blob(canvas, x, y, 5, 5, VERMILION, 1.0, rim=None)
    return canvas


def painting_mountain_pass():
    canvas = Canvas(256, 256)
    ground_wash(canvas, 128, 246, 112, 12)
    stamp_mountain(canvas, 80, 240, 128, 190, (96, 140, 130), (34, 60, 58), True)
    stamp_mountain(canvas, 182, 240, 116, 160, (110, 150, 136), (34, 60, 58), True)
    path = [(128, 248), (140, 220), (118, 196), (136, 170), (128, 150)]
    ink(canvas, path, 9, (150, 112, 70), 0.9)
    ink(canvas, path, 3, VERMILION, 0.9)
    return canvas


def painting_shrine():
    canvas = Canvas(256, 256)
    ground_wash(canvas, 128, 246, 100, 12)
    house(canvas, 128, 236, 100, 64, 1.0, (206, 192, 168), VERMILION)
    for x in (48, 208):
        ink(canvas, [(x, 244), (x, 60)], 14, VERMILION, 0.95)
    ink(canvas, [(30, 66), (226, 66)], 15, VERMILION, 0.95)
    ink(canvas, [(18, 42), (238, 42)], 14, WASH_INK, 0.9)
    ink(canvas, [(48, 110), (208, 110)], 8, WASH_INK, 0.85)
    return canvas


def painting_cave():
    canvas = Canvas(256, 256)
    ground_wash(canvas, 128, 246, 112, 12)
    blob(canvas, 128, 240, 112, 205, (122, 118, 110), 0.97, rim_alpha=0.85, rim_width=4, clip=240)
    blob(canvas, 128, 240, 46, 90, (30, 28, 30), 0.97, rim_alpha=0.9, rim_width=3, clip=240)
    for x, y in ((70, 130), (190, 140), (96, 70), (160, 80)):
        ink(canvas, [(x, y), (x + 14, y + 22), (x + 4, y + 40)], 3, WASH_INK, 0.5)
    return canvas


def painting_cliff():
    canvas = Canvas(256, 256)
    ground_wash(canvas, 128, 248, 108, 10)
    face = [(30, 244), (24, 90), (70, 70), (96, 40), (140, 52), (176, 36), (226, 80), (228, 244)]
    poly(canvas, face, (130, 124, 112), 0.97, width=4)
    for x in (62, 100, 140, 180, 206):
        ink(canvas, [(x, 70 + x % 23), (x - 8, 140), (x + 4, 190), (x - 4, 240)], 4, WASH_INK, 0.55)
    return canvas


def painting_corrupt_village():
    canvas = Canvas(256, 256)
    ground_wash(canvas, 128, 244, 112, 14)
    blob(canvas, 128, 150, 100, 90, (120, 96, 110), 0.25, rim=None)
    house(canvas, 76, 238, 90, 90, 1.0, (170, 160, 150), (56, 52, 58))
    house(canvas, 174, 236, 110, 120, 1.0, (160, 150, 142), (56, 52, 58))
    ink(canvas, [(130, 60), (220, 130)], 7, VERMILION, 0.95)
    ink(canvas, [(220, 60), (130, 130)], 7, VERMILION, 0.95)
    return canvas


SORT_PAINTINGS = {"haunted_forest": painting_haunted_forest, "mountain_pass": painting_mountain_pass,
                  "shrine": painting_shrine, "cave": painting_cave, "cliff": painting_cliff,
                  "corrupt_village": painting_corrupt_village}


# name -> (painter, opaque); the names are those in art/sprites.toml.
PLACEHOLDERS = {
    "ground": (ground, True),
    "rock_large": (lambda: rock(180, 128, 31), False),
    "rock_small": (lambda: rock(92, 68, 32), False),
    "pine": (pine, False),
    "shrine": (shrine, False),
    "fox": (fox, False),
    "cultivator": (cultivator, False),
    "villager": (villager, False),
    "orb_blue": (lambda: orb(32, (80, 196, 255)), False),
    "orb_red": (lambda: orb(16, (255, 96, 64)), False),
    "shadow": (shadow, False),
    "cell_plain": (cell_plain, True),
    "cell_mountain": (cell_mountain, True),
    "cell_forest": (cell_forest, True),
    "cell_city": (cell_city, True),
    "sword": (sword, False),
    "halo": (halo, False),
    "fire_bolt": (fire_bolt, False),
    "lance_strike": (lance_strike, False),
    "solid": (solid, False),
    "glow_core": (glow_core, False),
    **{f"rune_{i:02d}": ((lambda i=i: rune(i)), False) for i in range(RUNES)},
    **{f"flourish_flame_{i}": ((lambda i=i: flourish_flame(i)), False) for i in range(3)},
    "emblem_lotus": (emblem_lotus, False),
    "emblem_swords": (emblem_swords, False),
    "emblem_eye": (emblem_eye, False),
    "emblem_spiral": (emblem_spiral, False),
    **{f"road_{m}": ((lambda m=m: road(m)), False) for m in range(16)},
    "bridge_h": (lambda: plank_bridge("h"), False),
    "bridge_v": (lambda: plank_bridge("v"), False),
    "ford": (ford, False),
    **{f"marker_{sort}": ((lambda sort=sort: marker(sort)), False) for sort in GLYPHS},
    "route_dot": (route_dot, False),
    "route_marker": (route_marker, False),
    "target_tile": (target_tile, False),
    **{f"cloud_{i}": ((lambda i=i: cloud(i)), False) for i in range(CLOUDS)},
    **{f"stamp_{kind}_{i}": ((lambda kind=kind, i=i: stamp(kind, i)), False) for kind in STAMPS for i in range(4)},
    **{f"mist_{i}": ((lambda i=i: mist(i)), False) for i in range(4)},
    "painting_city": (lambda: painting_city(512, 0), False),
    "painting_capital": (painting_capital, False),
    **{f"painting_{sort}": (painter, False) for sort, painter in SORT_PAINTINGS.items()},
}


def main(names):
    OUT.mkdir(parents=True, exist_ok=True)
    for name in names or PLACEHOLDERS:
        painter, opaque = PLACEHOLDERS[name]
        painter().png(OUT / f"{name}.png", opaque)
        print(f"placeholders: assets/sprites/{name}.png")


if __name__ == "__main__":
    main(sys.argv[1:])
