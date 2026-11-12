"""Procedural placeholder art for the sprite pipeline (design doc 0002 §2, §9): ink wash on paper.

Writes assets/sprites/<name>.png for every placeholder sprite in art/sprites.toml: a painted
ground, rocks, a pine, a shrine, a fox, a cultivator, a villager, glowing orbs, a blob shadow, four world-map
cells, and the abilities' effects: a sword, a halo and a soft dot (both unused now), a fire bolt, the lance's burst
and the bars' solid square. Each is original and drawn from code. Reproducible byte for byte: the drawing uses only
arithmetic and square roots (correctly rounded everywhere), integer hashing, and seeded
random.Random, never a transcendental function. Run through `./dev atlas --placeholders`.

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
}


def main(names):
    OUT.mkdir(parents=True, exist_ok=True)
    for name in names or PLACEHOLDERS:
        painter, opaque = PLACEHOLDERS[name]
        painter().png(OUT / f"{name}.png", opaque)
        print(f"placeholders: assets/sprites/{name}.png")


if __name__ == "__main__":
    main(sys.argv[1:])
