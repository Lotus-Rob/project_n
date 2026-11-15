"""The world map's generator (phase 6 chunk 5b1, spec Goal 9): land formed the way real land forms.

Writes assets/data/world.yaml and two previews, build/world_map_preview.png (one pixel per tile, enlarged, with the
rivers, roads, fords, cities and danger sites drawn over it) and build/world_map_terrain.png (the ground alone).

world.yaml is hand-tuned after generation and is the source from then on: running this again starts over from these
inputs and loses every edit made to world.yaml by hand since. Run with `pixi run python art/world_map.py`.

How the land forms, in order:
- a hand-shaped continent: the sea as overlapping ellipses (the eastern sea, the south-east, a southern gulf) and the
  islands, their coasts made ragged by domain-warped fbm and frayed wherever they still run straight; the port's tile,
  which must hold WORLD_BUSIEST, is kept on land with the sea at its door;
- height: lowlands rising inland under rolling noise, ranges as ridges along hand-drawn spines (the great western range
  and its spurs, lesser ranges in the north and the south-east) shaped by ridged noise so they branch, the great river's
  valley carved from the western range to the eastern sea, lake basins, passes cut through the range;
- rivers by flow accumulation: depressions filled from the sea and the map's edges (deep ones left as lakes), water
  flowing down the steepest slope, rain gathered downstream; the largest flows become rivers, simplified to polylines;
- climate: temperature from latitude (cooler north) and height, moisture from latitude (wetter south), the nearness of
  water and a rain shadow east of the range (wind from the west);
- biomes from a Whittaker-style table onto the eleven kinds, then a smoothing pass: no kind in a patch under 3 tiles;
- cities at the hand sites below, roads as least-cost paths between them by the game's step rule (the entered tile's
  cost, 7/5 of it diagonally, past two passable neighbours) over a gently noisy cost so they wander like real roads,
  simplified (Ramer-Douglas-Peucker, one-tile tolerance) and checked tile by tile as the game rasterises them;
- fords where a stretch of land would otherwise be cut off by rivers, and danger sites by sort on fitting ground,
  off the roads and more of them at the wild edges.

Standard library only, like placeholders.py, whose noise it reuses: arithmetic and square roots, no transcendental
function, so a run gives the same map on every machine.
"""

import heapq
import itertools
import math
from pathlib import Path

from placeholders import Canvas, fbm, hash01, noise, smoothstep

ROOT = Path(__file__).resolve().parent.parent
YAML_OUT = ROOT / "assets" / "data" / "world.yaml"
STRINGS = ROOT / "assets" / "data" / "strings.yaml"
PREVIEW = ROOT / "build" / "world_map_preview.png"
TERRAIN_PREVIEW = ROOT / "build" / "world_map_terrain.png"

# The hand inputs.

SEED = 7
COLUMNS, ROWS, TILE_SIZE = 150, 85, 256  # game::WORLD_COLUMNS, WORLD_ROWS, WORLD_TILE
BUSIEST = (33_000, 10_800)  # game::WORLD_BUSIEST, the measurement path's anchor: the port's tile holds it
PORT = (BUSIEST[0] // TILE_SIZE, BUSIEST[1] // TILE_SIZE)
ROAD_COST, FORD_COST = 4, 30
IMPASSABLE = None

# The eleven kinds: name, symbol, walk cost (None: impassable), preview colour.
KINDS = [
    ("deep_water", "~", IMPASSABLE, (38, 66, 112)),
    ("shallows", "-", 30, (78, 128, 166)),
    ("sand", ":", 14, (222, 202, 146)),
    ("marsh", "%", 25, (96, 118, 86)),
    ("meadow", ".", 10, (138, 178, 92)),
    ("grassland", ",", 10, (188, 190, 112)),
    ("forest", "T", 16, (52, 98, 58)),
    ("hills", "n", 20, (156, 144, 98)),
    ("rock", "#", 40, (128, 122, 116)),
    ("snow", "*", 30, (236, 238, 244)),
    ("peaks", "A", IMPASSABLE, (82, 76, 88)),
]
KIND = {name: index for index, (name, _, _, _) in enumerate(KINDS)}
WATER = {KIND["deep_water"], KIND["shallows"]}

# The sea, as ellipses (centre x, y, radius x, y) in tiles; a tile is sea as far inside one as the coast's noise allows.
SEA = [
    (176.0, 42.0, 47.0, 47.0),  # the eastern sea: its coast passes just east of the port
    (150.0, 106.0, 50.0, 34.0),  # the south-east
    (84.0, 100.0, 26.0, 24.0),  # the southern gulf
    (66.0, 84.0, 10.0, 6.0),  # its western inlet, where the marsh's rivers reach it clear of the map's edge
    (166.0, -20.0, 36.0, 30.0),  # the north-east
]
# The islands, as ellipses, and the shoals that join them to the coast (segments the sea is shallow along).
ISLANDS = [
    (143.5, 58.0, 5.5, 4.5),  # the isle town's island
    (146.5, 46.0, 3.0, 4.0),
    (140.0, 20.0, 4.0, 3.0),
    (136.0, 74.0, 4.0, 2.6),
    (147.0, 67.0, 2.4, 2.4),
    (137.0, 31.0, 7.0, 3.0),  # a headland north of the port
]
SHOALS = [((131.0, 57.0), (139.0, 58.0)), ((144.0, 52.0), (146.0, 48.5))]

# Ranges: a spine polyline, its half-width in tiles and its height.
RANGES = [
    ([(13, -6), (20, 9), (11, 23), (21, 37), (12, 51), (23, 65), (14, 79), (20, 92)], 13.0, 0.78),  # the western range
    ([(19, 10), (30, 15), (40, 11), (48, 15)], 7.0, 0.66),  # a spur north of the great river
    ([(18, 34), (28, 29), (37, 31)], 6.0, 0.62),
    ([(14, 52), (26, 57), (36, 54), (45, 59)], 7.0, 0.68),  # a spur south of it
    ([(21, 66), (31, 72), (38, 70)], 6.0, 0.58),
    ([(52, 27), (64, 23), (78, 26), (92, 22), (104, 25)], 9.0, 0.36),  # the northern hills, between plains and steppe
    ([(96, 64), (106, 61), (115, 64), (120, 60)], 5.5, 0.44),  # the south-eastern hills
]
# The great river's course from the western range to the eastern sea (its valley is carved along it, so its water
# follows); the capital stands on its north bank at the map's centre, the port on its mouth's north side.
GREAT_RIVER = [(20, 44), (27, 43), (35, 46), (44, 43), (53, 47), (61, 44), (68, 46), (75, 44.0), (83, 45.5), (91, 42),
               (99, 45), (107, 43), (115, 46), (122, 44), (129, 44.5)]
# Tiles the great river is made to pass, inserted into its traced course: the capital's bank.
GREAT_RIVER_THROUGH = [(75, 43)]
# How far above its mouth the great river splits into a second channel to the sea, its delta, in tiles.
DELTA_SPLIT = 6
# Lake basins: centre, radius in tiles, depth.
OASIS = (63.0, 11.0, 2.6, 0.18)  # the oasis's lake, which also waters the desert round it
LAKES = [
    OASIS,
    (93.0, 55.0, 5.0, 0.26),  # a lake south of the great river
]
# Passes cut through the range (polylines whose height is capped below the peaks: rock and snow, never peaks).
PASSES = [
    [(9, 24), (16, 24), (24, 26)],
    [(10, 44), (17, 43), (24, 44)],
    [(12, 62), (19, 61), (26, 63)],
]

# The cities: key, tile, share of the 50,000 world NPCs, whether the game starts there. The capital first (the start),
# then the port (the busiest gathering), then the three towns the stand-in gatherings sit round, then the isle.
CITIES = [
    ("capital", (75, 42), 15_000, True),
    ("port", PORT, 12_000, False),
    ("sect", (24, 29), 6_000, False),
    ("oasis", (60, 13), 5_000, False),
    ("marsh_town", (65, 71), 7_000, False),
    ("isle", (143, 58), 5_000, False),
]
# The roads, between cities: each routed after the last, so later ones join earlier ones as real road networks grow.
ROADS = [
    ("capital", "port"),
    ("capital", "sect"),
    ("capital", "oasis"),
    ("capital", "marsh_town"),
    ("port", "isle"),
    ("marsh_town", "port"),
    ("oasis", "sect"),
]
# The danger sites: how many of each sort, and their names (key, Chinese, English) in the order they are placed.
DANGERS = {
    "mountain_pass": [("wolf_fang_pass", "狼牙关", "Wolf Fang Pass"), ("cloud_gate_pass", "云门关", "Cloud Gate Pass"),
                      ("frost_throat_pass", "霜喉关", "Frost Throat Pass")],
    "cave": [("serpent_cave", "蛇窟", "Serpent Cave"), ("echoing_cave", "回音洞", "Echoing Cave"),
             ("bat_cave", "蝠洞", "Bat Cave"), ("jade_cave", "玉髓洞", "Jade Cave")],
    "haunted_forest": [("weeping_woods", "泣林", "Weeping Woods"), ("ghost_pine_forest", "鬼松林", "Ghost Pine Forest"),
                       ("ghostlight_woods", "幽灯林", "Ghostlight Woods"), ("bone_forest", "白骨林", "Bone Forest")],
    "shrine": [("star_shrine", "观星祠", "Star Shrine"), ("fallen_immortal_shrine", "谪仙祠", "Fallen Immortal Shrine"),
               ("tide_shrine", "潮音祠", "Tide Shrine")],
    "cliff": [("sword_cliff", "剑崖", "Sword Cliff"), ("heavens_edge_cliff", "天涯崖", "Heaven's Edge Cliff"),
              ("eagle_cliff", "鹰崖", "Eagle Cliff"), ("white_crane_cliff", "白鹤崖", "White Crane Cliff")],
    "corrupt_village": [("blight_village", "疫村", "Blight Village"), ("rotting_reed_village", "腐苇村", "Rotting Reed Village")],
}
CITY_NAMES = {
    "capital": ("天京城", "Heavenly Capital"),
    "port": ("望海港", "Seawatch Harbour"),
    "sect": ("青云宗", "Azure Cloud Sect"),
    "oasis": ("月泉镇", "Moon Spring Town"),
    "marsh_town": ("雾泽镇", "Mist Marsh Town"),
    "isle": ("蓬莱岛", "Penglai Isle"),
}

RIVER_SHARE = 0.03  # the share of the most water any tile gathers that makes a river
LEAST_RIVER = 10  # tiles: a shorter tributary is left out
RIVER_TOLERANCE = 0.75  # tiles: how far a river's polyline may stray from the flow it follows
ROAD_TOLERANCE = 1.0  # tiles, the spec's
ROAD_PLAN_COST = 6.0  # what following a built road costs a planned one: cheap, so roads join, but not so cheap that
# every road detours through the capital
BRIDGE_PLAN_COST = 60  # what crossing a river costs a planned road, so roads cross seldom and straight across
CAUSEWAY_PLAN_COST = 90  # roads take to the shallows only where they must: the isle's causeway
FORDS = 5  # the most fords placed beyond those cut-off land needs; the detour test usually stops short of it
SHORE_WANDER = 14.0  # how far the coastline wanders in and out: up to half of it either way, in tiles
MOST_STRAIGHT_SHORE = 5  # tiles: a straighter run of shore is frayed by inlets and spits
DANGER_SPACING = 11  # tiles between a danger site and any other place
DANGER_ROAD_CLEARANCE = 3  # tiles between a danger site and the nearest road

NEIGHBOURS = [(-1, -1), (0, -1), (1, -1), (-1, 0), (1, 0), (-1, 1), (0, 1), (1, 1)]
ORTHOGONAL = [(0, -1), (1, 0), (0, 1), (-1, 0)]


def index(x, y):
    return y * COLUMNS + x


def on_map(x, y):
    return 0 <= x < COLUMNS and 0 <= y < ROWS


def clamp01(value):
    return max(0.0, min(1.0, value))


def falloff(distance, reach):
    """1 at distance 0, 0 from `reach` on, smooth between."""
    return smoothstep(reach, 0.0, distance) if distance < reach else 0.0


def segment_distance(px, py, ax, ay, bx, by):
    """The distance from (px, py) to the segment from a to b, and how far along it the nearest point lies (0 to 1)."""
    dx, dy = bx - ax, by - ay
    length = dx * dx + dy * dy
    t = 0.0 if length == 0 else clamp01(((px - ax) * dx + (py - ay) * dy) / length)
    nx, ny = ax + dx * t - px, ay + dy * t - py
    return math.sqrt(nx * nx + ny * ny), t


def polyline_distance(px, py, points):
    """The distance to a polyline and the fraction of its length at the nearest point."""
    lengths = [0.0]
    for (ax, ay), (bx, by) in itertools.pairwise(points):
        lengths.append(lengths[-1] + math.sqrt((bx - ax) ** 2 + (by - ay) ** 2))
    best, along = float("inf"), 0.0
    for segment, ((ax, ay), (bx, by)) in enumerate(itertools.pairwise(points)):
        distance, t = segment_distance(px, py, ax, ay, bx, by)
        if distance < best:
            best = distance
            along = (lengths[segment] + t * (lengths[segment + 1] - lengths[segment])) / lengths[-1]
    return best, along


def warp(x, y, seed, scale, amount):
    """(x, y) pushed about by smooth noise: domain warping, which bends straight noise features into organic ones."""
    return (x + (fbm(x * scale, y * scale, seed, 3) - 0.5) * amount,
            y + (fbm(x * scale + 31.7, y * scale + 17.3, seed + 7, 3) - 0.5) * amount)


def ridged(x, y, seed, octaves=5):
    """Ridged fbm in [0, 1]: sharp crests where plain noise crosses its middle, which branch like real ridges."""
    total, amplitude, norm = 0.0, 1.0, 0.0
    for octave in range(octaves):
        crest = 1.0 - abs(2.0 * noise(x, y, seed + octave * 131) - 1.0)
        total += crest * crest * amplitude
        norm += amplitude
        x, y, amplitude = x * 2.0, y * 2.0, amplitude * 0.5
    return total / norm


# The land.

def wander(px, py):
    """How far, in tiles, the coastline is pushed at (px, py): a smooth, low-frequency offset (one octave of noise per
    axis, a wavelength of about ten tiles), so the shore bulges into headlands and bays a few tiles deep over five to
    fifteen rows. Near the port it is evened out to nothing at its tile (fading over 6 tiles), so the shore still passes
    WORLD_BUSIEST's tile and bends away from it either side."""
    def push(x, y):
        return ((noise(x / 10.0, y / 10.0, SEED + 19) - 0.5) * SHORE_WANDER,
                (noise(x / 10.0, y / 10.0, SEED + 23) - 0.5) * SHORE_WANDER)

    ox, oy = push(px, py)
    near = falloff(math.sqrt((px - PORT[0] - 0.5) ** 2 + (py - PORT[1] - 0.5) ** 2), 6.0)
    port_x, port_y = push(PORT[0] + 0.5, PORT[1] + 0.5)
    return ox - port_x * near, oy - port_y * near


def coast_field():
    """Each tile's distance inland from the coast in tiles, roughly: negative at sea. The mainland's shape (the sea's
    ellipses, ragged by noise) is sampled where wander() pushes each tile, so its coastline wanders by that many tiles:
    where every term falls steeply towards the sea, adding an offset would barely move the shore, while moving where it
    is read moves the shore by exactly the offset. The islands are laid on afterwards, where they are, with their own
    noise, so the wander never joins them to the coast."""
    def ragged(px, py):
        return ((fbm(px / 7.0, py / 7.0, SEED + 13, 5) - 0.5) * 22.0
                + (fbm(px / 3.0, py / 3.0, SEED + 15, 3) - 0.5) * 8.0)  # coves and capes a tile or two across

    mainland = []
    for y in range(ROWS):
        for x in range(COLUMNS):
            px, py = x + 0.5, y + 0.5
            wx, wy = warp(px, py, SEED + 11, 1 / 18, 18.0)
            value = 1e9
            for cx, cy, rx, ry in SEA:
                ex, ey = (wx - cx) / rx, (wy - cy) / ry
                value = min(value, (math.sqrt(ex * ex + ey * ey) - 1.0) * (rx + ry) * 0.5)
            mainland.append(value + ragged(px, py))

    def read(fx, fy):  # the mainland's field between tile centres, bilinearly; clamped to the map
        fx, fy = max(0.0, min(COLUMNS - 1.0, fx)), max(0.0, min(ROWS - 1.0, fy))
        x0, y0 = min(int(fx), COLUMNS - 2), min(int(fy), ROWS - 2)
        tx, ty = fx - x0, fy - y0
        top = mainland[index(x0, y0)] * (1 - tx) + mainland[index(x0 + 1, y0)] * tx
        bottom = mainland[index(x0, y0 + 1)] * (1 - tx) + mainland[index(x0 + 1, y0 + 1)] * tx
        return top * (1 - ty) + bottom * ty

    field = []
    for y in range(ROWS):
        for x in range(COLUMNS):
            px, py = x + 0.5, y + 0.5
            ox, oy = wander(px, py)
            value = read(x + ox, y + oy)
            ix, iy = warp(px, py, SEED + 17, 1 / 6, 5.0)  # islands keep their own shapes, ragged at their own scale
            for cx, cy, rx, ry in ISLANDS:
                ex, ey = (ix - cx) / rx, (iy - cy) / ry
                value = max(value, (1.0 - math.sqrt(ex * ex + ey * ey)) * (rx + ry) * 0.5 + ragged(px, py))
            field.append(value)
    return field


def drop_specks(cells, least, keep_border=False):
    """Flips every 4-connected patch of True or False smaller than `least` tiles to its surroundings."""
    cells = cells[:]
    seen = [False] * len(cells)
    for start in range(len(cells)):
        if seen[start]:
            continue
        patch, stack, border = [], [start], False
        seen[start] = True
        while stack:
            tile = stack.pop()
            patch.append(tile)
            x, y = tile % COLUMNS, tile // COLUMNS
            border |= x in (0, COLUMNS - 1) or y in (0, ROWS - 1)
            for dx, dy in ORTHOGONAL:
                if on_map(x + dx, y + dy):
                    other = index(x + dx, y + dy)
                    if not seen[other] and cells[other] == cells[start]:
                        seen[other] = True
                        stack.append(other)
        if len(patch) < least and not (keep_border and border):
            for tile in patch:
                cells[tile] = not cells[tile]
    return cells


def harbour(land):
    """The land with the port's tile on it and the sea at its door, wherever the coast formed: a spit is laid west to it
    from the shore when the sea covers it, an inlet cut east to it from the open sea when it lies inland. Returns the land
    and the tiles to keep as they are (the port's and the sea beside it)."""
    land = land[:]
    px, py = PORT
    if not land[index(px, py)]:
        x = px
        while x >= 0 and not land[index(x, py)]:
            land[index(x, py)] = True
            x -= 1
    beside = [index(px + dx, py + dy) for dx, dy in NEIGHBOURS if on_map(px + dx, py + dy)]
    if all(land[t] for t in beside):
        x = px + 1
        while x < COLUMNS and land[index(x, py)]:
            land[index(x, py)] = False
            x += 1
    return land, {index(px, py)} | {t for t in beside if not land[t]}


def straight_shores(wet):
    """Every straight run of shore: consecutive (land tile, water tile) pairs along one row or column boundary, the
    water on the same side throughout."""
    runs = []
    for across in (True, False):  # boundaries between rows, then between columns
        lines, length = (ROWS - 1, COLUMNS) if across else (COLUMNS - 1, ROWS)
        for line in range(lines):
            step = 0
            while step < length:
                a, b = (index(step, line), index(step, line + 1)) if across else (index(line, step), index(line + 1, step))
                if wet[a] == wet[b]:
                    step += 1
                    continue
                side, pairs = wet[a], []
                while step < length:
                    a, b = (index(step, line), index(step, line + 1)) if across else (index(line, step), index(line + 1, step))
                    if wet[a] == wet[b] or wet[a] != side:
                        break
                    pairs.append((b, a) if side else (a, b))
                    step += 1
                runs.append(pairs)
    return runs


def fray_shore(land, keep):
    """The land with every run of shore longer than MOST_STRAIGHT_SHORE broken every three to five tiles by an inlet (land
    turned to sea) or a spit (sea turned to land), one to three tiles deep, each chosen by a hash of its tile: a smooth
    noise coast can still run straight where the field is steep, and real shores are jagged at every scale. Tiles in
    `keep` never change (the port's tile and the sea beside it)."""
    wet = [not tile for tile in land]
    for _ in range(12):
        straight = [pairs for pairs in straight_shores(wet) if len(pairs) > MOST_STRAIGHT_SHORE]
        if not straight:
            break
        for pairs in straight:
            at = 1 + int(hash01(pairs[0][0] % COLUMNS, pairs[0][0] // COLUMNS, SEED + 83) * 3)
            while at < len(pairs) - 1:
                shore, sea = pairs[at]
                x, y = shore % COLUMNS, shore // COLUMNS
                dx, dy = sea % COLUMNS - x, sea // COLUMNS - y  # towards the sea
                inlet = hash01(x, y, SEED + 81) < 0.5
                depth = 1 + int(hash01(x, y, SEED + 85) * 3)
                for reach in range(depth):
                    tx, ty = (x - dx * reach, y - dy * reach) if inlet else (x + dx * (reach + 1), y + dy * (reach + 1))
                    if on_map(tx, ty) and index(tx, ty) not in keep:
                        wet[index(tx, ty)] = inlet
                at += 3 + int(hash01(x, y, SEED + 87) * 3)
    return [not tile for tile in wet]


def height_field(coast, land):
    heights = []
    for y in range(ROWS):
        for x in range(COLUMNS):
            px, py = x + 0.5, y + 0.5
            c = coast[index(x, y)]
            wx, wy = warp(px, py, SEED + 21, 1 / 16, 10.0)
            inland = smoothstep(0.0, 28.0, c)
            # The land falls from the western range to the eastern sea, so its water runs east.
            tilt = 0.20 * (1.0 - px / COLUMNS)
            h = (0.06 + tilt + 0.14 * inland + (fbm(wx / 13.0, wy / 13.0, SEED + 23, 5) - 0.5) * 0.30
                 + (fbm(px / 3.0, py / 3.0, SEED + 25, 3) - 0.5) * 0.05  # small relief, which water winds round
                 # The land rises off the north and south edges, so water turns away from them to the sea or a lake.
                 + 0.08 * smoothstep(ROWS - 7.0, ROWS, py) + 0.08 * smoothstep(7.0, 0.0, py))
            crest = ridged(wx / 9.0, wy / 9.0, SEED + 29)
            mx, my = warp(px, py, SEED + 27, 1 / 24, 18.0)
            mountain = 0.0
            for spine, width, top in RANGES:
                distance, _ = polyline_distance(mx, my, spine)
                # A crest along the spine, broken where the ridged noise dips, and ridges branching off it.
                lift = 0.50 * falloff(distance, width * 0.35) * (0.4 + crest) + falloff(distance, width) * (0.12 + 0.8 * crest)
                mountain = max(mountain, top * lift)
            # Rolling country between the ranges: hills where the low-frequency noise says so.
            rolling = smoothstep(0.55, 0.75, fbm(wx / 24.0, wy / 24.0, SEED + 31, 3)) * 0.22 * crest
            h += mountain + rolling
            distance, along = polyline_distance(px, py, GREAT_RIVER)
            valley = falloff(distance, 5.0)
            floor = 0.40 * (1.0 - along) ** 2 + 0.04 + distance * 0.012
            h = h + (min(h, floor) - h) * valley
            for cx, cy, radius, depth in LAKES:
                d = math.sqrt((wx - cx) ** 2 + (wy - cy) ** 2) * 0.5 + math.sqrt((px - cx) ** 2 + (py - cy) ** 2) * 0.5
                h -= depth * falloff(d, radius * 1.8)
            for spine in PASSES:
                distance, _ = polyline_distance(px, py, spine)
                h = min(h, 0.70 + distance * 0.06)
            if not land[index(x, y)]:
                h = min(h, -0.05 + c * 0.02)
            heights.append(h)
    return heights


def fill_depressions(heights, land):
    """Priority flood from the sea and the map's west edge: each land tile raised to the level water would spill from it."""
    epsilon = 1e-5
    filled = heights[:]
    done = [False] * len(heights)
    queue = []
    for tile in range(len(heights)):
        x, y = tile % COLUMNS, tile // COLUMNS
        # Water leaves by the sea, or off the map's west edge, where the land runs on beyond the range; every river
        # east of the range ends in the sea, a lake or another river on the map.
        if not land[tile] or x == 0:
            done[tile] = True
            heapq.heappush(queue, (filled[tile], tile))
    while queue:
        level, tile = heapq.heappop(queue)
        x, y = tile % COLUMNS, tile // COLUMNS
        for dx, dy in NEIGHBOURS:
            if on_map(x + dx, y + dy):
                other = index(x + dx, y + dy)
                if not done[other]:
                    done[other] = True
                    filled[other] = max(filled[other], level + epsilon)
                    heapq.heappush(queue, (filled[other], other))
    return filled


def lakes_of(heights, filled, land):
    """Tiles under deep enough standing water, in patches big enough to be lakes rather than puddles."""
    wet = [land[t] and filled[t] - heights[t] > 0.02 for t in range(len(heights))]
    lake = [False] * len(heights)
    seen = [False] * len(heights)
    for start in range(len(heights)):
        if not wet[start] or seen[start]:
            continue
        patch, stack = [], [start]
        seen[start] = True
        while stack:
            tile = stack.pop()
            patch.append(tile)
            x, y = tile % COLUMNS, tile // COLUMNS
            for dx, dy in ORTHOGONAL:
                if on_map(x + dx, y + dy) and wet[index(x + dx, y + dy)] and not seen[index(x + dx, y + dy)]:
                    seen[index(x + dx, y + dy)] = True
                    stack.append(index(x + dx, y + dy))
        # Only the hand-placed basins hold lakes, and only within their reach: other hollows, and a basin's flooded
        # surroundings, fill and the water runs on across them, as it does in dry land.
        for cx, cy, radius, _ in LAKES:
            reach = (radius * 1.6) ** 2
            if any((tile % COLUMNS - cx) ** 2 + (tile // COLUMNS - cy) ** 2 <= radius * radius for tile in patch):
                for tile in patch:
                    if (tile % COLUMNS - cx) ** 2 + (tile // COLUMNS - cy) ** 2 <= reach:
                        lake[tile] = True
    return lake


def flow_directions(filled):
    """Each tile's downhill neighbour (steepest descent; diagonals by their length), or -1 at an outlet."""
    down = [-1] * len(filled)
    for tile in range(len(filled)):
        x, y = tile % COLUMNS, tile // COLUMNS
        best, steepest = -1, 0.0
        for dx, dy in NEIGHBOURS:
            if on_map(x + dx, y + dy):
                other = index(x + dx, y + dy)
                slope = (filled[tile] - filled[other]) / (1.4142135623730951 if dx and dy else 1.0)
                if slope > steepest:
                    best, steepest = other, slope
        down[tile] = best
    return down


def rain_field():
    """How much rain each tile gathers before the climate is known: wetter south, drier north."""
    return [0.35 + 0.65 * (t // COLUMNS) / (ROWS - 1) + (fbm(t % COLUMNS / 15, t // COLUMNS / 15, SEED + 41) - 0.5) * 0.4
            for t in range(COLUMNS * ROWS)]


def accumulate(filled, down, rain, land):
    gathered = [rain[t] if land[t] else 0.0 for t in range(len(filled))]
    for tile in sorted(range(len(filled)), key=lambda t: -filled[t]):
        if land[tile] and down[tile] >= 0:
            gathered[down[tile]] += gathered[tile]
    return gathered


def simplify(points, tolerance):
    """Ramer-Douglas-Peucker: the fewest of `points` keeping the line within `tolerance` of every one."""
    if len(points) < 3:
        return points[:]
    (ax, ay), (bx, by) = points[0], points[-1]
    worst, at = -1.0, 0
    for i in range(1, len(points) - 1):
        distance, _ = segment_distance(points[i][0], points[i][1], ax, ay, bx, by)
        if distance > worst:
            worst, at = distance, i
    if worst <= tolerance:
        return [points[0], points[-1]]
    return simplify(points[:at + 1], tolerance)[:-1] + simplify(points[at:], tolerance)


def trace_rivers(gathered, down, land, lake):
    """The rivers as tile paths, each from its source down to the sea, a lake, the map's edge or the river it joins:
    the largest first, each stem followed upstream along its biggest branch, so a river's main course is one line."""
    least = max(gathered) * RIVER_SHARE
    river = [land[t] and not lake[t] and gathered[t] >= least for t in range(len(gathered))]
    upstream = [[] for _ in gathered]
    for tile in range(len(gathered)):
        if river[tile] and down[tile] >= 0 and river[down[tile]]:
            upstream[down[tile]].append(tile)
    mouths = [t for t in range(len(gathered)) if river[t] and (down[t] < 0 or not river[down[t]])]
    mouths.sort(key=lambda t: -gathered[t])
    paths = []

    def follow(start):
        # Upstream from `start` along the biggest branch, then each other branch met on the way.
        stem = [start]
        while upstream[stem[-1]]:
            stem.append(max(upstream[stem[-1]], key=lambda t: gathered[t]))
        stem.reverse()  # source first
        return stem

    pending = [(mouth, None) for mouth in mouths]
    while pending:
        start, junction = pending.pop(0)
        stem = follow(start)
        if junction is not None and len(stem) < LEAST_RIVER:
            continue
        if junction is None and len(stem) < LEAST_RIVER * 2:
            continue
        paths.append(stem + ([junction] if junction is not None else []))
        for tile in stem:
            branches = [b for b in upstream[tile] if b not in stem]
            branches.sort(key=lambda t: -gathered[t])
            pending.extend((branch, tile) for branch in branches)
    return paths


def through(line, points):
    """`line` made to pass each of `points`, each inserted into the segment nearest it, whichever way the line runs
    there; a point more than 2 tiles from every segment is refused (the river has moved away from it)."""
    line = line[:]
    for px, py in points:
        distance, at = min((segment_distance(px, py, *line[i], *line[i + 1])[0], i) for i in range(len(line) - 1))
        if distance > 2.0:
            raise SystemExit(f"world_map: the great river passes {distance:.1f} tiles from {(px, py)}; move the point")
        line.insert(at + 1, (px, py))
    return line


def despike(line):
    """`line` without the vertices that only kink it over a short span (a bump of a tile or so within three tiles),
    which read as wiggles rather than meanders."""
    line = line[:]
    i = 1
    while i < len(line) - 1:
        (ax, ay), (bx, by), (cx, cy) = line[i - 1], line[i], line[i + 1]
        if max(abs(cx - ax), abs(cy - ay)) <= 3 and segment_distance(bx, by, ax, ay, cx, cy)[0] <= 1.0:
            del line[i]
        else:
            i += 1
    return line


def join(rivers):
    """Each river that ends on another's course made to pass that tile, so a tributary meets the river it flows into
    after both are simplified."""
    rivers = [line[:] for line in rivers]
    for number, line in enumerate(rivers):
        end = line[-1]
        for other, course in enumerate(rivers):
            if other == number:
                continue
            distance, at = min((segment_distance(*end, *course[i], *course[i + 1])[0], i) for i in range(len(course) - 1))
            if distance <= 1.5 and end not in line_tiles(course):
                course.insert(at + 1, end)
                break
    return rivers


def check_rivers(rivers, land, lake):
    """Refuses a river that touches the map's edge other than the west (where the land runs on), or whose end meets
    neither the sea, a lake nor another river."""
    tiles = [line_tiles(line) for line in rivers]
    for number, line in enumerate(tiles):
        edge = [(x, y) for x, y in line if x == COLUMNS - 1 or y in (0, ROWS - 1)]
        if edge:
            raise SystemExit(f"world_map: river {number} runs on the map's edge at {edge[0]}")
        x, y = line[-1]
        beside = [index(x + dx, y + dy) for dx, dy in NEIGHBOURS if on_map(x + dx, y + dy)]
        others = {t for other, course in enumerate(tiles) if other != number for t in course}
        if x != 0 and not any(not land[t] or lake[t] for t in beside) and (x, y) not in others:
            raise SystemExit(f"world_map: river {number} ends at {(x, y)}, short of the sea, a lake or another river")


def great_river_path(paths):
    """The traced path that is the great river: the one whose mouth lies at GREAT_RIVER's end (paths come largest
    first, and the largest reaching that mouth is the great river)."""
    mx, my = GREAT_RIVER[-1]
    for number, path in enumerate(paths):
        x, y = path[-1] % COLUMNS, path[-1] // COLUMNS
        if max(abs(x - mx), abs(y - my)) <= 4:
            return number
    raise SystemExit(f"world_map: no river reaches the great river's mouth at {GREAT_RIVER[-1]}; reshape its valley")


def delta(path, land, lake):
    """A second channel from DELTA_SPLIT tiles above the river's mouth to the sea, on the far side from the first."""
    sea = [not land[t] for t in range(len(land))]
    to_sea = distances_from(sea)
    channel = set(path)
    start = path[-1 - DELTA_SPLIT]
    mouth = path[-1]
    side = 1 if (start // COLUMNS) >= (mouth // COLUMNS) else -1  # away from where the main channel bends
    line = [start]
    while to_sea[line[-1]] > 1:
        x, y = line[-1] % COLUMNS, line[-1] // COLUMNS
        options = []
        for dx, dy in ((1, 0), (0, side), (1, side)):
            if on_map(x + dx, y + dy):
                other = index(x + dx, y + dy)
                if land[other] and not lake[other] and other not in channel:
                    options.append((to_sea[other], -dy * side, other))
        if not options:
            break
        line.append(min(options)[2])
        channel.add(line[-1])
    return simplify([(t % COLUMNS, t // COLUMNS) for t in line], RIVER_TOLERANCE)


def rasterise(first, second):
    """Every tile the segment passes through, 4-connected, exactly as core::WorldMap's Rasterise marks them."""
    if (second[1], second[0]) < (first[1], first[0]):
        first, second = second, first
    dx, dy = second[0] - first[0], second[1] - first[1]
    step_x, step_y = (-1 if dx < 0 else 1), (-1 if dy < 0 else 1)
    length_x, length_y = abs(dx), abs(dy)
    x, y = first
    done_x = done_y = 0
    tiles = [(x, y)]
    while done_x < length_x or done_y < length_y:
        across_x = (2 * done_x + 1) * length_y
        across_y = (2 * done_y + 1) * length_x
        if done_y >= length_y or (done_x < length_x and across_x <= across_y):
            x += step_x
            done_x += 1
        else:
            y += step_y
            done_y += 1
        tiles.append((x, y))
    return tiles


def line_tiles(line):
    tiles = []
    for point in range(len(line)):
        for tile in rasterise(line[point], line[point + 1] if point + 1 < len(line) else line[point]):
            if not tiles or tiles[-1] != tile:
                tiles.append(tile)
    return tiles


# The climate and the ground.

def distances_from(sources, passable=None):
    """Steps (8-connected) from the nearest source tile."""
    far = 10 ** 9
    distance = [far] * (COLUMNS * ROWS)
    frontier = [t for t in range(COLUMNS * ROWS) if sources[t]]
    for tile in frontier:
        distance[tile] = 0
    while frontier:
        following = []
        for tile in frontier:
            x, y = tile % COLUMNS, tile // COLUMNS
            for dx, dy in NEIGHBOURS:
                if on_map(x + dx, y + dy):
                    other = index(x + dx, y + dy)
                    if distance[other] == far and (passable is None or passable[other]):
                        distance[other] = distance[tile] + 1
                        following.append(other)
        frontier = following
    return distance


def rain_shadow(heights):
    """How dry the wind from the west leaves each tile: high ground upwind wrings the water out of it."""
    shadow = [0.0] * len(heights)
    for y in range(ROWS):
        highest = 0.0
        for x in range(COLUMNS):
            h = heights[index(x, y)]
            highest = max(h, highest * 0.965)  # the wind gathers water again as it crosses the lowlands
            shadow[index(x, y)] = clamp01((highest - h - 0.12) * 2.2)
    return shadow


def climate(heights, land, lake, river_tiles):
    """Temperature and moisture per tile, and each tile's steps from the nearest water."""
    water = [not land[t] or lake[t] or river_tiles[t] for t in range(len(heights))]
    near_water = distances_from(water)
    sea = distances_from([not land[t] for t in range(len(heights))])
    shadow = rain_shadow(heights)
    temperature, moisture = [], []
    for y in range(ROWS):
        for x in range(COLUMNS):
            t = index(x, y)
            # Latitude bent by noise, so the climate's bands wander instead of running straight across.
            _, wy = warp(x + 0.5, y + 0.5, SEED + 49, 1 / 24, 40.0)
            latitude = clamp01(wy / (ROWS - 1))
            h = max(0.0, heights[t])
            temperature.append(0.12 + 0.80 * latitude - 0.95 * max(0.0, h - 0.30)
                               + (fbm(x / 20, y / 20, SEED + 51) - 0.5) * 0.15)
            oasis = falloff(math.sqrt((x - OASIS[0]) ** 2 + (y - OASIS[1]) ** 2), OASIS[2] + 4.0)
            wet = (0.06 + 0.62 * latitude * (0.6 + 0.4 * latitude)
                   + 0.14 * falloff(near_water[t], 5.0)
                   + 0.12 * falloff(sea[t], 20.0)
                   - 0.34 * shadow[t]
                   + 0.55 * oasis
                   + (fbm(x / 10, y / 10, SEED + 53, 5) - 0.5) * 0.50
                   + (fbm(x / 22, y / 22, SEED + 55, 3) - 0.5) * 0.60)
            moisture.append(clamp01(wet))
    return temperature, moisture, near_water


def biome(h, temperature, moisture, coastal, beach, near_water, grove):
    if h > 0.84:
        return KIND["peaks"]
    if h > 0.70:
        return KIND["snow"] if temperature < 0.45 else KIND["rock"]
    if h > 0.60:
        return KIND["rock"]
    if h > 0.47:
        return KIND["forest"] if moisture > 0.60 else KIND["hills"]
    if coastal and h < 0.24 and beach:
        return KIND["marsh"] if moisture > 0.70 and temperature > 0.6 else KIND["sand"]
    if moisture > 0.64 and h < 0.22 and temperature > 0.55 and near_water <= 4:
        return KIND["marsh"]
    if moisture < 0.18:
        return KIND["sand"]
    if moisture < 0.32:
        return KIND["grassland"]
    if moisture < 0.56:
        if grove > 0.60:
            return KIND["forest"]
        if grove < 0.36:
            return KIND["grassland"]
        return KIND["meadow"] if temperature > 0.28 else KIND["grassland"]
    return KIND["forest"]


def ground(heights, land, lake, temperature, moisture, near_water):
    sea = [not land[t] for t in range(len(heights))]
    from_sea = distances_from(sea)
    from_land = distances_from([land[t] and not lake[t] for t in range(len(heights))])
    kinds = []
    for t in range(len(heights)):
        x, y = t % COLUMNS, t // COLUMNS
        if sea[t] or lake[t]:
            # A lake is shallow only at its rim, so its middle can't be waded across.
            shallow = 1 if lake[t] else 1.5 + 1.6 * fbm(x / 6, y / 6, SEED + 61)
            depth = from_land[t]
            for (ax, ay), (bx, by) in SHOALS:
                if segment_distance(x + 0.5, y + 0.5, ax, ay, bx, by)[0] < 1.6:
                    depth = 1
            kinds.append(KIND["shallows"] if depth <= shallow else KIND["deep_water"])
            continue
        beach = noise(x / 3.5, y / 3.5, SEED + 63) > 0.35
        grove = fbm(x / 6.0, y / 6.0, SEED + 65, 4)  # woods and open grass scattered over the plains
        # The height bands' edges frayed by fine noise, so no contour of a smooth slope runs straight for long.
        frayed = heights[t] + (fbm(x / 2.5, y / 2.5, SEED + 67, 3) - 0.5) * 0.10
        # How far from water marsh reaches, wandering too, so its inland edge doesn't trace the shore at a fixed distance.
        wading = near_water[t] + (fbm(x / 5.0, y / 5.0, SEED + 69, 2) - 0.5) * 8.0
        kinds.append(biome(frayed, temperature[t], moisture[t], from_sea[t] <= 1, beach, wading, grove))
    return kinds


def smooth_ground(kinds, rounds=2):
    """Majority smoothing within land and within water, then no patch of a kind under 3 tiles."""
    for _ in range(rounds):
        smoothed = kinds[:]
        for t in range(len(kinds)):
            x, y = t % COLUMNS, t // COLUMNS
            counts = {}
            for dx, dy in NEIGHBOURS:
                if on_map(x + dx, y + dy):
                    other = kinds[index(x + dx, y + dy)]
                    if (other in WATER) == (kinds[t] in WATER):
                        counts[other] = counts.get(other, 0) + 1
            if counts:
                common = max(sorted(counts), key=lambda k: counts[k])
                if counts[common] >= 5 and common != kinds[t]:
                    smoothed[t] = common
        kinds = smoothed
    for _ in range(8):
        changed = False
        seen = [False] * len(kinds)
        for start in range(len(kinds)):
            if seen[start]:
                continue
            patch, stack = [], [start]
            seen[start] = True
            while stack:
                tile = stack.pop()
                patch.append(tile)
                x, y = tile % COLUMNS, tile // COLUMNS
                for dx, dy in ORTHOGONAL:
                    if on_map(x + dx, y + dy):
                        other = index(x + dx, y + dy)
                        if not seen[other] and kinds[other] == kinds[start]:
                            seen[other] = True
                            stack.append(other)
            if len(patch) >= 3:
                continue
            counts = {}
            for tile in patch:
                x, y = tile % COLUMNS, tile // COLUMNS
                for dx, dy in NEIGHBOURS:
                    if on_map(x + dx, y + dy):
                        other = kinds[index(x + dx, y + dy)]
                        if other != kinds[start] and (other in WATER) == (kinds[start] in WATER):
                            counts[other] = counts.get(other, 0) + 1
            if counts:
                common = max(sorted(counts), key=lambda k: counts[k])
                for tile in patch:
                    kinds[tile] = common
                changed = True
        if not changed:
            break
    return kinds


# The game's rules, mirrored: what a tile costs once roads, rivers and fords are on it.

def kind_cost(kind):
    return KINDS[kind][2]


def derive(kinds, roads, rivers, fords):
    """Per tile: the walk cost (None: impassable) and the road, river, bridge and ford flags, as WorldMap::Build."""
    river = [False] * len(kinds)
    road = [False] * len(kinds)
    for line in rivers:
        for x, y in line_tiles(line):
            river[index(x, y)] = True
    for line in roads:
        for x, y in line_tiles(line):
            if kind_cost(kinds[index(x, y)]) is None:
                raise SystemExit(f"world_map: a road crosses impassable ({x}, {y})")
            road[index(x, y)] = True
    ford = [False] * len(kinds)
    for x, y in fords:
        t = index(x, y)
        if not river[t] or road[t] or kind_cost(kinds[t]) is None:
            raise SystemExit(f"world_map: the ford at ({x}, {y}) isn't on an open, unbridged river")
        ford[t] = True
    cost = []
    for t, kind in enumerate(kinds):
        c = kind_cost(kind)
        if road[t]:
            c = ROAD_COST if c is None else min(c, ROAD_COST)
        elif river[t]:
            c = FORD_COST if ford[t] else None
        cost.append(c)
    return cost, road, river, ford


def step_cost(cost, tile, other):
    """core::WorldMap::StepCost: the entered tile's cost, 7/5 of it diagonally past two passable neighbours."""
    if cost[other] is None:
        return None
    x, y, ox, oy = tile % COLUMNS, tile // COLUMNS, other % COLUMNS, other // COLUMNS
    if x == ox or y == oy:
        return cost[other]
    if cost[index(ox, y)] is None or cost[index(x, oy)] is None:
        return None
    return cost[other] * 7 // 5


def reachable(cost, start):
    seen = [False] * len(cost)
    seen[start] = True
    stack = [start]
    while stack:
        tile = stack.pop()
        x, y = tile % COLUMNS, tile // COLUMNS
        for dx, dy in NEIGHBOURS:
            if on_map(x + dx, y + dy):
                other = index(x + dx, y + dy)
                if not seen[other] and step_cost(cost, tile, other) is not None:
                    seen[other] = True
                    stack.append(other)
    return seen


# Roads.

def plan_costs(kinds, river, road):
    """What entering each tile costs a planned road: the ground's cost, gently varied so roads wander as real ones do,
    an existing road's cost, a bridge's high cost, or None."""
    costs = []
    for t, kind in enumerate(kinds):
        c = kind_cost(kind)
        if c is None:
            costs.append(None)
        elif road[t]:
            costs.append(ROAD_PLAN_COST)
        elif river[t]:
            costs.append(float(BRIDGE_PLAN_COST))
        elif kind == KIND["shallows"]:
            costs.append(float(CAUSEWAY_PLAN_COST))
        else:
            x, y = t % COLUMNS, t // COLUMNS
            costs.append(c * (0.5 + 1.3 * fbm(x / 8, y / 8, SEED + 71, 3)))
    return costs


def route(costs, river, start, goal):
    """The cheapest path from start to goal by the game's step rule over `costs`; a diagonal step never cuts past a
    river's corner (the rasterised road would bridge it)."""
    best = {start: 0.0}
    came = {}
    queue = [(0.0, start)]
    while queue:
        spent, tile = heapq.heappop(queue)
        if tile == goal:
            break
        if spent > best[tile]:
            continue
        x, y = tile % COLUMNS, tile // COLUMNS
        for dx, dy in NEIGHBOURS:
            if not on_map(x + dx, y + dy):
                continue
            other = index(x + dx, y + dy)
            c = costs[other]
            if c is None:
                continue
            if dx and dy:
                corners = (index(x + dx, y), index(x, y + dy))
                if any(costs[k] is None or river[k] for k in corners):
                    continue
                c *= 1.4
            if spent + c < best.get(other, float("inf")):
                best[other] = spent + c
                came[other] = tile
                heapq.heappush(queue, (spent + c, other))
    if goal not in best:
        raise SystemExit(f"world_map: no road can join tiles {start} and {goal}")
    path = [goal]
    while path[-1] != start:
        path.append(came[path[-1]])
    path.reverse()
    return path


def fit_line(path, kinds, river, costs):
    """A polyline over `path` (tile indices) as a road would be built: pulled straight wherever a straight stretch costs
    about what the path's winding one does (so it bends only round what is worth avoiding), then simplified
    (Ramer-Douglas-Peucker at the spec's tolerance). Every rasterised tile stays passable, and it bridges no river the
    path didn't."""
    points = [(t % COLUMNS, t // COLUMNS) for t in path]
    allowed = set(points)
    spent = [0.0]
    for a, b in itertools.pairwise(path):
        diagonal = a % COLUMNS != b % COLUMNS and a // COLUMNS != b // COLUMNS
        spent.append(spent[-1] + costs[b] * (1.4 if diagonal else 1.0))

    def fits(a, b):
        for x, y in rasterise(a, b):
            t = index(x, y)
            if kind_cost(kinds[t]) is None or (river[t] and (x, y) not in allowed):
                return False
        return True

    def straight(a, b):
        tiles = rasterise(a, b)[1:]
        mean = sum(costs[index(x, y)] for x, y in tiles) / max(1, len(tiles))
        return mean * math.sqrt((b[0] - a[0]) ** 2 + (b[1] - a[1]) ** 2)

    pulled = [0]
    while pulled[-1] < len(points) - 1:
        i = pulled[-1]
        best = i + 1
        for j in range(i + 2, min(len(points), i + 18)):
            if not fits(points[i], points[j]):
                break
            if straight(points[i], points[j]) <= (spent[j] - spent[i]) * 1.02 + 0.5:
                best = j
        pulled.append(best)
    corners = [points[k] for k in pulled]
    kept = simplify(corners, ROAD_TOLERANCE)
    line = [kept[0]]
    for a, b in itertools.pairwise(kept):
        if fits(a, b):
            line.append(b)
        else:
            line.extend(corners[corners.index(a) + 1:corners.index(b) + 1])
    return line


def build_roads(kinds, river, places):
    road = [False] * len(kinds)
    lines = []
    for first, second in ROADS:
        costs = plan_costs(kinds, river, road)
        a, b = places[first], places[second]
        path = route(costs, river, index(*a), index(*b))
        # Only the stretches off the roads already built become new roads, each joined to the road it leaves.
        run = []
        for step, tile in enumerate(path):
            if not road[tile]:
                if not run and step > 0:
                    run.append(path[step - 1])
                run.append(tile)
            elif run:
                run.append(tile)
                lines.append(fit_line(run, kinds, river, costs))
                run = []
        if run:
            lines.append(fit_line(run, kinds, river, costs))
        for line in lines:
            for x, y in line_tiles(line):
                road[index(x, y)] = True
    return lines


# Fords and danger sites.

def walking_costs(cost, start):
    """The cheapest walk from `start` to every tile by the step rule (None where none reaches)."""
    best = [None] * len(cost)
    best[start] = 0
    queue = [(0, start)]
    while queue:
        spent, tile = heapq.heappop(queue)
        if spent > best[tile]:
            continue
        x, y = tile % COLUMNS, tile // COLUMNS
        for dx, dy in NEIGHBOURS:
            if on_map(x + dx, y + dy):
                other = index(x + dx, y + dy)
                step = step_cost(cost, tile, other)
                if step is not None and (best[other] is None or spent + step < best[other]):
                    best[other] = spent + step
                    heapq.heappush(queue, (spent + step, other))
    return best


def add_fords(kinds, roads, rivers, capital):
    """Fords where people would wade: on open river tiles far from any bridge, where the walk from one bank to the other
    otherwise goes far round (the difference of the banks' walks from the capital stands in for the detour); first any
    stretch of land rivers cut off, then FORDS more, spaced apart."""
    fords = []
    for _ in range(FORDS + 20):
        cost, road, river, _ = derive(kinds, roads, rivers, fords)
        walk = walking_costs(cost, index(*capital))
        road_distance = distances_from(road)
        stranded = any(cost[t] is not None and walk[t] is None for t in range(len(cost)))
        if not stranded and len(fords) >= FORDS:
            break
        best = None
        for t in range(len(cost)):
            x, y = t % COLUMNS, t // COLUMNS
            if not river[t] or road[t] or kind_cost(kinds[t]) is None or road_distance[t] < 6:
                continue
            if any(max(abs(x - fx), abs(y - fy)) < 12 for fx, fy in fords):
                continue
            for (ax, ay), (bx, by) in (((0, -1), (0, 1)), ((-1, 0), (1, 0))):
                if not (on_map(x + ax, y + ay) and on_map(x + bx, y + by)):
                    continue
                a, b = index(x + ax, y + ay), index(x + bx, y + by)
                if cost[a] is None or cost[b] is None:
                    continue
                if (walk[a] is None) == (walk[b] is None):
                    detour = 0 if walk[a] is None else abs(walk[a] - walk[b])
                else:
                    detour = 10 ** 6  # one bank can't be reached at all
                if best is None or (detour, -t) > best[0]:
                    best = ((detour, -t), t)
        if best is None or best[0][0] < 200:
            break
        fords.append((best[1] % COLUMNS, best[1] // COLUMNS))
    return fords


def danger_candidates(sort, kinds, heights, road_distance, city_distance, forest_depth, steepness, coastal):
    """Tiles fit for a danger site of `sort`, each with a score (higher is better)."""
    found = []
    for t, kind in enumerate(kinds):
        x, y = t % COLUMNS, t // COLUMNS
        if kind_cost(kind) is None or road_distance[t] < DANGER_ROAD_CLEARANCE or not (2 <= x < COLUMNS - 2) or not (
                2 <= y < ROWS - 2):
            continue
        wild = min(city_distance[t], 40) / 40.0
        h = heights[t]
        if sort == "mountain_pass":
            if kind not in (KIND["rock"], KIND["snow"]):
                continue
            walls = sum(1 for dx, dy in NEIGHBOURS for r in (1, 2)
                        if on_map(x + dx * r, y + dy * r) and kinds[index(x + dx * r, y + dy * r)] == KIND["peaks"])
            if walls < 4:
                continue
            score = walls + wild * 4
        elif sort == "cave":
            if kind not in (KIND["hills"], KIND["rock"]):
                continue
            score = steepness[t] * 30 + wild * 3
        elif sort == "haunted_forest":
            if kind != KIND["forest"]:
                continue
            score = forest_depth[t] + wild * 3
        elif sort == "shrine":
            if kind in (KIND["marsh"], KIND["shallows"], KIND["sand"]) or h > 0.62:
                continue
            higher = sum(1 for dx, dy in NEIGHBOURS for r in (1, 2, 3)
                         if on_map(x + dx * r, y + dy * r) and heights[index(x + dx * r, y + dy * r)] > h)
            if higher > 1:
                continue
            score = h * 6 + wild * 2 + (2 if coastal[t] else 0)
        elif sort == "cliff":
            if kind not in (KIND["rock"], KIND["hills"], KIND["snow"]):
                continue
            score = steepness[t] * 40 + (6 if coastal[t] else 0) + wild * 2
        else:  # corrupt_village
            if kind != KIND["marsh"]:
                continue
            score = sum(1 for dx, dy in NEIGHBOURS if kinds[index(x + dx, y + dy)] == KIND["marsh"]) + wild * 3
        found.append((score, t))
    found.sort(key=lambda s: (-s[0], s[1]))
    return found


def place_dangers(kinds, heights, cost, roads, places, capital):
    road_tiles = [False] * len(kinds)
    for line in roads:
        for x, y in line_tiles(line):
            road_tiles[index(x, y)] = True
    road_distance = distances_from(road_tiles)
    city_tiles = [False] * len(kinds)
    for x, y in places.values():
        city_tiles[index(x, y)] = True
    city_distance = distances_from(city_tiles)
    forest_depth = distances_from([k != KIND["forest"] for k in kinds])
    coastal = [False] * len(kinds)
    steepness = [0.0] * len(kinds)
    for t in range(len(kinds)):
        x, y = t % COLUMNS, t // COLUMNS
        for dx, dy in ORTHOGONAL:
            if on_map(x + dx, y + dy):
                other = index(x + dx, y + dy)
                coastal[t] |= kinds[other] in WATER and kinds[t] not in WATER
                steepness[t] = max(steepness[t], abs(heights[t] - heights[other]))
    reach = reachable(cost, index(*capital))
    taken = [index(x, y) for x, y in places.values()]
    dangers = []
    # Interleave the sorts so each gets its pick of the ground in turn.
    order = []
    for round_ in range(max(len(names) for names in DANGERS.values())):
        order.extend((sort, names[round_]) for sort, names in DANGERS.items() if round_ < len(names))
    for sort, (key, _, _) in order:
        best = None
        for score, t in danger_candidates(sort, kinds, heights, road_distance, city_distance, forest_depth, steepness,
                                          coastal):
            x, y = t % COLUMNS, t // COLUMNS
            if not reach[t] or cost[t] is None:
                continue
            nearest = min(max(abs(x - o % COLUMNS), abs(y - o // COLUMNS)) for o in taken)
            if nearest < DANGER_SPACING:
                continue
            # Spread them out: a site loses for every other danger near it, so no corner of the map hoards them.
            crowd = sum(1 for _, _, (ox, oy) in dangers if max(abs(x - ox), abs(y - oy)) < 24)
            ranked = (score - 2.5 * crowd, -t)
            if best is None or ranked > best[0]:
                best = (ranked, t)
        if best is None:
            print(f"world_map: no ground for {sort} {key}")
            continue
        dangers.append((key, sort, (best[1] % COLUMNS, best[1] // COLUMNS)))
        taken.append(best[1])
    return dangers


# Output.

def flow_list(points, indent):
    """A YAML flow list of [column, row] points, wrapped to lines of about 120 characters."""
    items = [f"[{x}, {y}]" for x, y in points]
    lines, current = [], "["
    for i, item in enumerate(items):
        piece = item + (", " if i + 1 < len(items) else "]")
        if len(current) + len(piece) > 118 - indent and current != "[":
            lines.append(current.rstrip())
            current = " " * (indent + 1)
        current += piece
    lines.append(current)
    return ("\n" + " " * indent).join(lines)


def write_yaml(kinds, rivers, roads, fords, cities, dangers):
    out = [
        "# The world map (phase 6 chunk 5b1): game data, hashed, read once at the start and not reloaded.",
        "# Generated by art/world_map.py from real land forming, then hand-tuned: this file is the source from then on",
        "# (running the script again starts over and loses the hand edits). Columns run west to east, rows north to south.",
        f"road_cost: {ROAD_COST}",
        f"ford_cost: {FORD_COST}",
        f"tile_size: {TILE_SIZE}",
        "kinds:",
    ]
    for name, symbol, cost, _ in KINDS:
        out.append(f'  - {{ symbol: "{symbol}", name: {name}, cost: {"impassable" if cost is None else cost} }}')
    out.append("grid: |")
    for y in range(ROWS):
        out.append("  " + "".join(KINDS[kinds[index(x, y)]][1] for x in range(COLUMNS)))
    out.append("# Each a polyline of [column, row] tile points; a road over a river is a bridge.")
    out.append("roads:")
    out.extend("  - " + flow_list(line, 4) for line in roads)
    out.append("rivers:")
    out.extend("  - " + flow_list(line, 4) for line in rivers)
    out.append("# River tiles walked across without a bridge.")
    out.append("fords:")
    out.extend(f"  - [{x}, {y}]" for x, y in fords)
    out.append("places:")
    for key, tile, people, start in cities:
        out.append(f"  - {{ key: {key}, kind: city, tile: [{tile[0]}, {tile[1]}], name: place_{key}, people: {people}"
                   + (", start: true }" if start else " }"))
    for key, sort, tile in dangers:
        out.append(f"  - {{ key: {key}, kind: danger, sort: {sort}, tile: [{tile[0]}, {tile[1]}], name: place_{key} }}")
    YAML_OUT.write_text("\n".join(out) + "\n", encoding="utf-8")


def write_previews(kinds, road, river, ford, cities, dangers, scale=6):
    PREVIEW.parent.mkdir(parents=True, exist_ok=True)
    for path, overlays in ((TERRAIN_PREVIEW, False), (PREVIEW, True)):
        canvas = Canvas(COLUMNS * scale, ROWS * scale, (0, 0, 0))

        def fill(x0, y0, x1, y1, colour, canvas=canvas):
            for py in range(max(0, y0), min(canvas.height, y1)):
                for px in range(max(0, x0), min(canvas.width, x1)):
                    canvas.over(py * canvas.width + px, colour, 1.0)

        for t, kind in enumerate(kinds):
            x, y = t % COLUMNS, t // COLUMNS
            fill(x * scale, y * scale, (x + 1) * scale, (y + 1) * scale, KINDS[kind][3])
        if overlays:
            for layer, colour, width in ((river, (40, 100, 200), 2), (road, (150, 60, 40), 2)):
                for t in range(len(kinds)):
                    if not layer[t]:
                        continue
                    x, y = t % COLUMNS, t // COLUMNS
                    cx, cy = x * scale + scale // 2 - 1, y * scale + scale // 2 - 1
                    fill(cx, cy, cx + width, cy + width, colour)
                    for dx, dy in ORTHOGONAL:
                        if on_map(x + dx, y + dy) and layer[index(x + dx, y + dy)]:
                            fill(min(cx, cx + dx * scale // 2), min(cy, cy + dy * scale // 2),
                                 max(cx, cx + dx * scale // 2) + width, max(cy, cy + dy * scale // 2) + width, colour)
            for t in range(len(kinds)):
                if ford[t]:
                    x, y = t % COLUMNS, t // COLUMNS
                    fill(x * scale + 1, y * scale + 1, (x + 1) * scale - 1, (y + 1) * scale - 1, (250, 230, 60))
            for _, (x, y), _, _ in cities:
                fill(x * scale - scale, y * scale - scale, (x + 2) * scale, (y + 2) * scale, (20, 20, 20))
                fill(x * scale - scale + 2, y * scale - scale + 2, (x + 2) * scale - 2, (y + 2) * scale - 2, (250, 250, 250))
            for _, _, (x, y) in dangers:
                fill(x * scale - 2, y * scale - 2, (x + 1) * scale + 2, (y + 1) * scale + 2, (20, 20, 20))
                fill(x * scale, y * scale, (x + 1) * scale, (y + 1) * scale, (220, 30, 140))
        canvas.png(path, opaque=True)


def check_strings(cities, dangers):
    text = STRINGS.read_text(encoding="utf-8")
    missing = [key for key, *_ in cities + dangers if f"\nplace_{key}:" not in text]
    for key in missing:
        names = CITY_NAMES.get(key) or next((zh, en) for sort in DANGERS.values() for k, zh, en in sort if k == key)
        print(f"world_map: strings.yaml lacks place_{key} ({names[1]}, {names[0]})")


def settle(key, tile, kinds, river_tiles):
    """The city's tile, or the nearest walkable one off the rivers and the water when the land formed otherwise there
    (the capital and the port never move: the map's centre and WORLD_BUSIEST's tile)."""
    def fit(x, y):
        t = index(x, y)
        return kind_cost(kinds[t]) is not None and kinds[t] not in WATER and not river_tiles[t]

    if fit(*tile):
        return tile
    if key in ("capital", "port"):
        raise SystemExit(f"world_map: {key}'s tile {tile} isn't walkable land off the rivers; reshape the inputs")
    found = min(((x - tile[0]) ** 2 + (y - tile[1]) ** 2, y, x) for y in range(ROWS) for x in range(COLUMNS) if fit(x, y))
    print(f"world_map: {key} moved from {tile} to {(found[2], found[1])}, the nearest walkable land")
    return (found[2], found[1])


def main():
    print("world_map: generating; this starts over and overwrites assets/data/world.yaml and its hand edits")
    coast = coast_field()
    land = drop_specks([c > 0.0 for c in coast], 6)
    # Sea cut off from the ocean is land (lakes come from basins); land, however small, stays (islands).
    land = [a or b for a, b in zip(land, drop_specks(land, 400, keep_border=True))]
    land, harbour_tiles = harbour(land)
    land = fray_shore(land, harbour_tiles)
    for x, y in [tile for _, tile, _, _ in CITIES]:
        land[index(x, y)] = True
    heights = height_field(coast, land)
    filled = fill_depressions(heights, land)
    lake = lakes_of(heights, filled, land)
    down = flow_directions(filled)
    gathered = accumulate(filled, down, rain_field(), land)
    paths = trace_rivers(gathered, down, land, lake)
    city_tiles = {index(*tile) for _, tile, _, _ in CITIES}
    rivers = []
    for path in paths:
        points = [(t % COLUMNS, t // COLUMNS) for t in path]
        rivers.append(despike(simplify(points, RIVER_TOLERANCE)))
    great = great_river_path(paths)
    rivers[great] = through(rivers[great], GREAT_RIVER_THROUGH)
    rivers.append(delta(paths[great], land, lake))
    rivers = join(rivers)
    check_rivers(rivers, land, lake)
    river_tiles = [False] * (COLUMNS * ROWS)
    for line in rivers:
        for x, y in line_tiles(line):
            river_tiles[index(x, y)] = True
    on_city = [t for t in city_tiles if river_tiles[t]]
    if on_city:
        print(f"world_map: a river runs over a city's tile: {[(t % COLUMNS, t // COLUMNS) for t in on_city]}")
    temperature, moisture, near_water = climate(heights, land, lake, river_tiles)
    kinds = smooth_ground(ground(heights, land, lake, temperature, moisture, near_water))
    places = {key: settle(key, tile, kinds, river_tiles) for key, tile, _, _ in CITIES}
    px, py = places["port"]
    if not any(on_map(px + dx, py + dy) and not land[index(px + dx, py + dy)] for dx, dy in NEIGHBOURS):
        raise SystemExit(f"world_map: the port's tile {places['port']} isn't on the coast; reshape the sea round it")
    cities = [(key, places[key], people, start) for key, _, people, start in CITIES]
    roads = build_roads(kinds, river_tiles, places)
    capital = places["capital"]
    fords = add_fords(kinds, roads, rivers, capital)
    cost, road, river, ford = derive(kinds, roads, rivers, fords)
    reach = reachable(cost, index(*capital))
    for key, tile in places.items():
        if not reach[index(*tile)]:
            print(f"world_map: city {key} can't be reached from the capital")
    dangers = place_dangers(kinds, heights, cost, roads, places, capital)
    write_yaml(kinds, rivers, roads, fords, cities, dangers)
    write_previews(kinds, road, river, ford, cities, dangers)
    check_strings(cities, dangers)
    stranded = sum(1 for t in range(len(cost)) if cost[t] is not None and not reach[t])
    for key, sort, (x, y) in dangers:
        print(f"world_map: {sort} {key} at ({x}, {y}) on {KINDS[kinds[index(x, y)]][0]}")
    counts = {KINDS[k][0]: kinds.count(k) for k in range(len(KINDS))}
    shores = straight_shores([k in WATER for k in kinds])
    near = [len(p) for p in shores if any(max(abs(t % COLUMNS - PORT[0]), abs(t // COLUMNS - PORT[1])) <= 12 for t, _ in p)]
    print(f"world_map: the longest straight shore is {max(len(p) for p in shores)} tiles, {max(near)} within 12 of the port")
    print(f"world_map: {len(rivers)} rivers, {len(roads)} roads, {len(fords)} fords, {len(dangers)} danger sites, "
          f"{stranded} passable tiles out of reach; kinds {counts}")
    print(f"world_map: wrote {YAML_OUT.relative_to(ROOT)}, {PREVIEW.relative_to(ROOT)}, {TERRAIN_PREVIEW.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
