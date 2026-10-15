"""Generate PRISM's original, quiet synthesizer feedback using the standard library."""

import math
from pathlib import Path
import struct
import wave


def write_tone(path, notes, duration):
    rate = 22050
    frames = bytearray()

    for i in range(int(rate * duration)):
        t = i / rate
        value = 0.0
        for frequency, start in notes:
            age = t - start

            if age >= 0:
                envelope = min(1.0, age * 140) * math.exp(-age * 13)
                tone = math.sin(2 * math.pi * frequency * age)
                tone += 0.22 * math.sin(4 * math.pi * frequency * age)
                value += tone * envelope * 0.22
        frames.extend(struct.pack("<h", int(max(-1, min(1, value)) * 32767)))

    with wave.open(str(path), "wb") as output:
        output.setnchannels(1)
        output.setsampwidth(2)
        output.setframerate(rate)
        output.writeframes(frames)

    with wave.open(str(path), "rb") as check:
        assert check.getnframes() == int(rate * duration)
        assert check.getnchannels() == 1


def main():
    target = Path(__file__).resolve().parents[1] / "Assets" / "Audio"
    target.mkdir(parents=True, exist_ok=True)

    sounds = {
        "move": ([(440, 0)], 0.07),
        "rotate": ([(659.25, 0), (987.77, 0.035)], 0.17),
        "lock": ([(164.81, 0), (329.63, 0.015)], 0.22),
        "clear": ([(523.25, 0), (659.25, 0.055), (783.99, 0.11)], 0.4),
        "tetris": ([(523.25, 0), (659.25, 0.055), (783.99, 0.11), (1046.5, 0.17)], 0.6),
        "over": ([(392, 0), (329.63, 0.12), (261.63, 0.24)], 0.65),
    }

    for name, (notes, duration) in sounds.items():
        write_tone(target / (name + ".wav"), notes, duration)

    print("PASS: six generated PCM sound effects")


if __name__ == "__main__":
    main()
