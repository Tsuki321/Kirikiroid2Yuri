#!/usr/bin/env python3
"""Generate original, deterministic movie fixtures and validate their streams."""

import argparse
import json
from pathlib import Path
import subprocess

from create_xp3_fixtures import container, file_index


def generate(destination):
    destination.mkdir(parents=True, exist_ok=True)
    # Full-frame colors make the native layer's actual decoded pixels observable
    # without depending on screenshots, font rasterization, or exact YUV rounding.
    video = ("color=c=red:s=64x64:r=10:d=3,"
             "drawbox=c=lime:t=fill:enable='gte(t,1)',"
             "drawbox=c=blue:t=fill:enable='gte(t,2)'")
    cases = [
        ("test.avi", ["-an", "-c:v", "mpeg4", "-g", "10"], None),
        ("test-av.avi", ["-c:v", "mpeg4", "-g", "10", "-c:a", "pcm_s16le"], "pcm_s16le"),
        ("test-delayed.mp4", ["-c:v", "libx264", "-g", "10", "-bf", "2",
                              "-x264-params", "b-adapt=0:scenecut=0", "-c:a", "aac"], "aac"),
    ]
    for name, encoding, audio_codec in cases:
        target = destination / name
        command = ["ffmpeg", "-nostdin", "-hide_banner", "-loglevel", "error", "-y",
                   "-f", "lavfi", "-i", video]
        if audio_codec:
            command += ["-f", "lavfi", "-i", "sine=frequency=440:sample_rate=44100:duration=3"]
        command += encoding + ["-pix_fmt", "yuv420p", "-threads", "1", str(target)]
        subprocess.run(command, check=True)
        streams = json.loads(subprocess.check_output([
            "ffprobe", "-v", "error", "-show_streams", "-of", "json", str(target)]))["streams"]
        picture = next(stream for stream in streams if stream["codec_type"] == "video")
        assert picture["width"] == 64 and picture["height"] == 64, name
        assert int(picture["nb_frames"]) == 30, name
        assert picture["pix_fmt"] == "yuv420p", name
        audio = [stream for stream in streams if stream["codec_type"] == "audio"]
        assert len(audio) == int(audio_codec is not None), name
        if audio_codec:
            assert audio[0]["codec_name"] == audio_codec, name
        if name.endswith(".mp4"):
            assert picture["codec_name"] == "h264" and picture["has_b_frames"] > 0, name
        pixels = subprocess.check_output([
            "ffmpeg", "-nostdin", "-v", "error", "-i", str(target), "-an",
            "-vf", "select='eq(n,0)+eq(n,10)+eq(n,29)'", "-vsync", "0",
            "-f", "rawvideo", "-pix_fmt", "rgb24", "-"])
        assert len(pixels) == 3 * 64 * 64 * 3, name
        for frame, channel in enumerate((0, 1, 2)):
            offset = frame * 64 * 64 * 3 + (32 * 64 + 32) * 3
            rgb = pixels[offset:offset + 3]
            assert rgb[channel] > 200 and all(rgb[c] < 40 for c in range(3) if c != channel), (name, rgb)
        print(f"Validated {name}: 30 frames, red/green/blue pixels, audio={audio_codec}")
    # MP4 probing and seeking must work through the XP3 IStream adapter too.
    # Keep its payload uncompressed to exercise sequential reads after a seek.
    data = (destination / "test-delayed.mp4").read_bytes()
    (destination / "movie-assets.xp3").write_bytes(
        container(file_index(data, name="test-delayed.mp4"), data))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("destination", type=Path)
    generate(parser.parse_args().destination)
