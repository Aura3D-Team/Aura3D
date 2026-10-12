#!/usr/bin/env python3
"""gen_sandbox_textures.py - regenerate the Sandbox demo textures.

The Sandbox scene loads its textures from resources/textures/ at runtime. They
are generated procedurally rather than committed as opaque binaries so that a
change to the palette or pattern is a reviewable diff, and so the repo does not
carry art assets it has no license story for.

Outputs (512x512 RGBA PNG, deterministic for a fixed seed):
  resources/textures/crate.png  - wooden crate, applied to the Sandbox cubes
  resources/textures/orb.png    - warm cobblestone in the same palette, applied
                                  to the Sandbox sphere; tiles seamlessly in U
                                  so it wraps a sphere without a visible seam

Usage:
  ./scripts/gen_sandbox_textures.py

Prerequisites: numpy, Pillow.
"""

from pathlib import Path

import numpy as np
from PIL import Image

SIZE = 512
SEED = 20260803

# One warm wood/stone palette shared by both textures, so the crate and the
# sphere read as belonging to the same scene rather than two unrelated assets.
WOOD_LIGHT = np.array([182, 142, 96], dtype=np.float32)
WOOD_MID = np.array([146, 106, 66], dtype=np.float32)
WOOD_DARK = np.array([96, 66, 38], dtype=np.float32)
WOOD_DEEP = np.array([62, 42, 24], dtype=np.float32)
STONE_LIGHT = np.array([170, 140, 104], dtype=np.float32)
STONE_DARK = np.array([104, 82, 58], dtype=np.float32)
MORTAR = np.array([74, 58, 42], dtype=np.float32)


def value_noise(shape, cells, rng, tileable=False):
    """Smooth value noise in [0,1] from a bilinearly upsampled random grid.

    `tileable` duplicates the first row/column of control points at the far
    edge, which makes the result wrap seamlessly - needed for anything mapped
    onto a sphere, where U=0 and U=1 are the same meridian.
    """
    grid = rng.random((cells + 1, cells + 1)).astype(np.float32)
    if tileable:
        grid[-1, :] = grid[0, :]
        grid[:, -1] = grid[:, 0]

    # Bilinear sample the control grid up to full resolution.
    ys = np.linspace(0, cells, shape[0], endpoint=False, dtype=np.float32)
    xs = np.linspace(0, cells, shape[1], endpoint=False, dtype=np.float32)
    y0, x0 = np.floor(ys).astype(int), np.floor(xs).astype(int)
    fy, fx = ys - y0, xs - x0

    # Smoothstep the interpolants so cell boundaries are not visible as creases.
    fy = (fy * fy * (3.0 - 2.0 * fy))[:, None]
    fx = (fx * fx * (3.0 - 2.0 * fx))[None, :]

    g00 = grid[np.ix_(y0, x0)]
    g01 = grid[np.ix_(y0, x0 + 1)]
    g10 = grid[np.ix_(y0 + 1, x0)]
    g11 = grid[np.ix_(y0 + 1, x0 + 1)]

    top = g00 + (g01 - g00) * fx
    bottom = g10 + (g11 - g10) * fx
    return top + (bottom - top) * fy


def fbm(shape, rng, octaves=4, base_cells=4, tileable=False):
    """Fractal sum of value noise: each octave doubles frequency, halves weight."""
    total = np.zeros(shape, dtype=np.float32)
    amplitude = 1.0
    weight = 0.0
    for octave in range(octaves):
        total += amplitude * value_noise(shape, base_cells * (2 ** octave), rng, tileable)
        weight += amplitude
        amplitude *= 0.5
    return total / weight


def lerp_color(a, b, t):
    """Blend two RGB triples by a per-pixel factor t in [0,1]."""
    return a[None, None, :] + (b - a)[None, None, :] * t[:, :, None]


def make_crate():
    """Plank-and-frame wooden crate: the classic six-plank face with a border."""
    rng = np.random.default_rng(SEED)
    yy, xx = np.mgrid[0:SIZE, 0:SIZE].astype(np.float32)
    u, v = xx / SIZE, yy / SIZE

    # Wood grain: near-vertical rings, warped by noise so they wander like real
    # grain instead of reading as printed stripes.
    warp = fbm((SIZE, SIZE), rng, octaves=4, base_cells=3)
    rings = np.sin((u * 26.0 + warp * 5.5) * np.pi * 2.0) * 0.5 + 0.5
    fibre = fbm((SIZE, SIZE), rng, octaves=5, base_cells=24)
    grain = np.clip(rings * 0.55 + fibre * 0.45, 0.0, 1.0)

    img = lerp_color(WOOD_MID, WOOD_LIGHT, grain)

    # Six vertical planks with a dark gap between each.
    planks = 6
    plank_u = (u * planks) % 1.0
    gap = np.minimum(plank_u, 1.0 - plank_u)
    seam = np.clip(gap / 0.035, 0.0, 1.0)
    img = lerp_color(WOOD_DEEP, np.array([255, 255, 255], dtype=np.float32), seam) / 255.0 * img

    # Per-plank tone variation so no two boards are the same shade.
    plank_index = np.floor(u * planks).astype(int)
    plank_shade = (rng.random(planks).astype(np.float32) * 0.22 + 0.89)[plank_index]
    img *= plank_shade[:, :, None]

    # Frame: border rail plus the diagonal brace a crate face is built with.
    border = 0.085
    in_border = (u < border) | (u > 1.0 - border) | (v < border) | (v > 1.0 - border)
    diag_a = np.abs((u - v)) < 0.055
    diag_b = np.abs((u + v - 1.0)) < 0.055
    frame = in_border | diag_a | diag_b

    frame_grain = fbm((SIZE, SIZE), rng, octaves=4, base_cells=10)
    frame_color = lerp_color(WOOD_DARK, WOOD_MID, frame_grain * 0.7)
    img = np.where(frame[:, :, None], frame_color, img)

    # Bevel: light from the top-left, so the frame reads as raised timber.
    edge = np.zeros((SIZE, SIZE), dtype=np.float32)
    for band, lo, hi in ((0.012, border - 0.012, border),
                         (0.012, 1.0 - border, 1.0 - border + 0.012)):
        edge += ((u > lo) & (u < hi)).astype(np.float32) * (1.0 if lo < 0.5 else -1.0)
        edge += ((v > lo) & (v < hi)).astype(np.float32) * (1.0 if lo < 0.5 else -1.0)
    img *= (1.0 + np.clip(edge, -1.0, 1.0) * 0.18)[:, :, None]

    # Iron nail heads in the frame corners.
    for cy in (border / 2, 1.0 - border / 2):
        for cx in (border / 2, 1.0 - border / 2):
            d = np.sqrt((u - cx) ** 2 + (v - cy) ** 2)
            head = np.clip(1.0 - d / 0.016, 0.0, 1.0)
            shade = np.array([92, 88, 84], dtype=np.float32)
            img = img * (1.0 - head[:, :, None]) + shade[None, None, :] * head[:, :, None]

    # Corner darkening: cheap baked ambient occlusion, keeps the face from
    # looking flat under the Sandbox's single directional light.
    vignette = 1.0 - 0.28 * ((np.abs(u - 0.5) * 2) ** 3 + (np.abs(v - 0.5) * 2) ** 3)
    img *= np.clip(vignette, 0.0, 1.0)[:, :, None]

    return img


def make_orb():
    """Warm cobblestone for the sphere - seamless in U so the wrap has no seam."""
    rng = np.random.default_rng(SEED + 1)
    yy, xx = np.mgrid[0:SIZE, 0:SIZE].astype(np.float32)
    u, v = xx / SIZE, yy / SIZE

    # Voronoi cells: distance to nearest and second-nearest seed. Seeds are
    # duplicated one period left and right so cells straddling U=0/U=1 match up.
    cell_count = 90
    seeds = rng.random((cell_count, 2)).astype(np.float32)
    seeds = np.concatenate([seeds - [1.0, 0.0], seeds, seeds + [1.0, 0.0]], axis=0)

    du = u[None, :, :] - seeds[:, 0][:, None, None]
    dv = v[None, :, :] - seeds[:, 1][:, None, None]
    dist = np.sqrt(du * du + dv * dv)
    nearest = np.partition(dist, 1, axis=0)[:2]
    d1, d2 = nearest[0], nearest[1]

    # The gap between the two nearest seeds is ~0 exactly on a cell border,
    # which is where the mortar goes.
    edge = np.clip((d2 - d1) / 0.045, 0.0, 1.0)

    speckle = fbm((SIZE, SIZE), rng, octaves=5, base_cells=16, tileable=True)
    stone = lerp_color(STONE_DARK, STONE_LIGHT, np.clip(speckle * 1.15, 0.0, 1.0))

    img = lerp_color(MORTAR, np.array([255, 255, 255], dtype=np.float32), edge) / 255.0 * stone

    # Round each cobble slightly: brighter at the centre, darker at the rim.
    img *= (0.82 + 0.32 * edge)[:, :, None]

    return img


def write_png(rgb, path):
    rgba = np.dstack([np.clip(rgb, 0, 255).astype(np.uint8),
                      np.full((SIZE, SIZE), 255, dtype=np.uint8)])
    Image.fromarray(rgba, mode="RGBA").save(path, optimize=True)
    print(f"Wrote {path}")


def main():
    out_dir = Path(__file__).resolve().parent.parent / "resources" / "textures"
    out_dir.mkdir(parents=True, exist_ok=True)

    write_png(make_crate(), out_dir / "crate.png")
    write_png(make_orb(), out_dir / "orb.png")


if __name__ == "__main__":
    main()
