#!/usr/bin/env python3
"""Generate a static HDR10 fixture for ambient-light validation using FFmpeg.

Top row: neutral patches at 0, 1, 10, 100, 203, 400, 1000 and 4000 cd/m².
Bottom row: BT.2020 red, green, blue, cyan, magenta, yellow, white, black;
each lit component is 203 cd/m². These are signal references, not a display
calibration. Inspect decoded-frame metadata, since some FFmpeg versions do
not expose the HEVC transfer/primaries in stream-level ffprobe output.
"""

import argparse
import struct
import subprocess


def pq_code(nits):
    luminance = (nits / 10000.0) ** (2610.0 / 16384.0)
    code = ((3424.0 / 4096.0 + 2413.0 / 128.0 * luminance)
            / (1.0 + 2392.0 / 128.0 * luminance)) ** (2523.0 / 32.0)
    return round(code * 65535)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", help="destination .mkv file (must not exist)")
    args = parser.parse_args()
    levels = (0, 1, 10, 100, 203, 400, 1000, 4000)
    colors = ((1, 0, 0), (0, 1, 0), (0, 0, 1), (0, 1, 1),
              (1, 0, 1), (1, 1, 0), (1, 1, 1), (0, 0, 0))
    gray = b"".join(struct.pack("<HHH", *([pq_code(n)] * 3)) * 120 for n in levels)
    color = b"".join(struct.pack("<HHH", *(pq_code(c * 203) for c in rgb)) * 120
                     for rgb in colors)
    subprocess.run([
        "ffmpeg", "-v", "error", "-n", "-f", "rawvideo",
        "-pixel_format", "rgb48le", "-video_size", "960x540", "-framerate", "1",
        "-i", "pipe:0", "-vf",
        "format=gbrp16le,zscale=matrixin=0:matrix=9:rangein=full:range=limited,format=yuv420p10le",
        "-c:v", "libx265", "-x265-params",
        "lossless=1:colorprim=9:transfer=16:colormatrix=9:log-level=error",
        "-color_primaries", "bt2020", "-color_trc", "smpte2084",
        "-colorspace", "bt2020nc", args.output,
    ], input=gray * 270 + color * 270, check=True)


if __name__ == "__main__":
    main()
