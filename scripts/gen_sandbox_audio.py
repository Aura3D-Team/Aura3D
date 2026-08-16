#!/usr/bin/env python3
"""Generate the Sandbox demo's audio assets.

The engine has no audio files checked in, for the same reason it has no
textures: they are generated, not authored, and a binary blob in git that a
script can reproduce exactly is a blob that will drift from the script.
AudioClipLoader substitutes silence for anything missing, so the demo runs
without these -- it is simply inaudible.

Writes 16-bit PCM WAV, the format AudioClipLoader parses in-tree.

Usage:
    python3 scripts/gen_sandbox_audio.py [output_dir]

Defaults to resources/audio/ relative to the repository root.
"""

from __future__ import annotations

import math
import pathlib
import struct
import sys

SAMPLE_RATE = 48000
CHANNELS = 1  # Mono: only a mono source spatializes meaningfully (see AudioEngine::mix).


def write_wav(path: pathlib.Path, frames: list[float], sample_rate: int = SAMPLE_RATE,
              channels: int = CHANNELS) -> None:
    """Writes `frames` (floats in [-1, 1]) as a 16-bit PCM WAV."""
    pcm = b"".join(
        struct.pack("<h", max(-32768, min(32767, int(round(value * 32767.0)))))
        for value in frames
    )

    bits_per_sample = 16
    byte_rate = sample_rate * channels * bits_per_sample // 8
    block_align = channels * bits_per_sample // 8

    header = b"".join([
        b"RIFF",
        struct.pack("<I", 36 + len(pcm)),
        b"WAVE",
        b"fmt ",
        struct.pack("<I", 16),          # PCM fmt chunk size
        struct.pack("<H", 1),           # WAVE_FORMAT_PCM
        struct.pack("<H", channels),
        struct.pack("<I", sample_rate),
        struct.pack("<I", byte_rate),
        struct.pack("<H", block_align),
        struct.pack("<H", bits_per_sample),
        b"data",
        struct.pack("<I", len(pcm)),
    ])

    path.write_bytes(header + pcm)
    duration = len(frames) / channels / sample_rate
    print(f"  {path.name}: {duration:.2f}s, {sample_rate} Hz, {channels} ch, {len(pcm)} bytes")


def envelope(index: int, total: int, attack: float = 0.01, release: float = 0.25) -> float:
    """Attack/release shaping, so a clip neither clicks on nor clicks off.

    A tone that starts or stops at a non-zero sample is a step discontinuity,
    which is audible as a click regardless of how clean the tone itself is.
    """
    t = index / total
    attack_gain = min(1.0, t / attack) if attack > 0 else 1.0
    release_gain = min(1.0, (1.0 - t) / release) if release > 0 else 1.0
    return attack_gain * release_gain


def make_tone(frequency: float, duration: float, amplitude: float = 0.4,
              harmonics: tuple[float, ...] = (1.0,)) -> list[float]:
    """A shaped tone, optionally with harmonics for a less synthetic timbre."""
    total = int(duration * SAMPLE_RATE)
    normalize = sum(harmonics)

    out = []
    for i in range(total):
        t = i / SAMPLE_RATE
        value = sum(
            weight * math.sin(2.0 * math.pi * frequency * (harmonic + 1) * t)
            for harmonic, weight in enumerate(harmonics)
        ) / normalize
        out.append(value * amplitude * envelope(i, total))
    return out


def make_blip() -> list[float]:
    """Short UI/interaction blip: a rising two-tone chirp."""
    duration = 0.12
    total = int(duration * SAMPLE_RATE)

    out = []
    for i in range(total):
        t = i / SAMPLE_RATE
        # Sweep 660 -> 990 Hz over the clip.
        frequency = 660.0 + (990.0 - 660.0) * (i / total)
        out.append(math.sin(2.0 * math.pi * frequency * t) * 0.35 * envelope(i, total, 0.02, 0.4))
    return out


def make_hum() -> list[float]:
    """Looping drone for the orb: the 3D-positional demo source.

    Held at a whole number of cycles so the loop point lands exactly on a zero
    crossing -- otherwise every repeat would click.
    """
    frequency = 220.0
    cycles = 44                     # 44 cycles at 220 Hz = exactly 0.2 s
    duration = cycles / frequency
    total = int(duration * SAMPLE_RATE)

    out = []
    for i in range(total):
        t = i / SAMPLE_RATE
        fundamental = math.sin(2.0 * math.pi * frequency * t)
        fifth = 0.3 * math.sin(2.0 * math.pi * frequency * 1.5 * t)
        out.append((fundamental + fifth) / 1.3 * 0.5)
    return out


def make_music() -> list[float]:
    """A short looping arpeggio, standing in for a streamed music track."""
    # A minor pentatonic figure: musical enough to be recognisably "music",
    # short enough to stay a generated asset rather than a recording.
    notes = [220.00, 261.63, 293.66, 349.23, 293.66, 261.63]
    note_duration = 0.28

    out: list[float] = []
    for frequency in notes:
        # Harmonic weights give a plucked rather than pure-sine character.
        out.extend(make_tone(frequency, note_duration, amplitude=0.28,
                             harmonics=(1.0, 0.5, 0.25)))
    return out


def main() -> int:
    if len(sys.argv) > 2:
        print(__doc__)
        return 2

    if len(sys.argv) == 2:
        out_dir = pathlib.Path(sys.argv[1])
    else:
        out_dir = pathlib.Path(__file__).resolve().parent.parent / "resources" / "audio"

    out_dir.mkdir(parents=True, exist_ok=True)
    print(f"Generating Sandbox audio into {out_dir}")

    write_wav(out_dir / "blip.wav", make_blip())
    write_wav(out_dir / "orb_hum.wav", make_hum())
    write_wav(out_dir / "music.wav", make_music())

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
