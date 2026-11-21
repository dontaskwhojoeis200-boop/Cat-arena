#!/usr/bin/env python3
"""N10-Q3E stage D part 6: Cat Arena's sounds and soundtrack for Quake3e.

  convert_sounds.py <repo root>

1. Builds tools/q3e/sound_bake.cpp against N10's audio code (native/src/audio_codec.cpp,
   audio_mixer.cpp) with the C++ compiler in $CXX (default g++) and runs it: the 48 N10 samples
   (assets/n7/audio/sfx) become 86 WAV files in game/base/catarena/sound/catarena with N10's levels,
   bus levels, voice tones and filters baked in (see sound_bake.cpp), plus sounds.txt (name,
   frames at 44.1 kHz, peak).
2. Converts the soundtrack (assets/n7/audio/main-theme.mp3, "ASHEN DRIVE") to Ogg Vorbis for
   Quake III's background track: game/base/catarena/music/ashen_drive.ogg (ffmpeg, libvorbis q4).
Both outputs are deterministic for the same tools; they are committed like the converted maps.
"""
import os
import struct
import subprocess
import sys
import tempfile


def main(root):
    out = os.path.join(root, "game", "base", "catarena")
    with tempfile.TemporaryDirectory() as tmp:
        exe = os.path.join(tmp, "sound_bake")
        cxx = os.environ.get("CXX", "g++")
        subprocess.check_call([cxx, "-std=c++20", "-O2", "-I", os.path.join(root, "native", "include"),
                               os.path.join(root, "tools", "q3e", "sound_bake.cpp"),
                               os.path.join(root, "native", "src", "audio_codec.cpp"),
                               os.path.join(root, "native", "src", "audio_mixer.cpp"), "-o", exe])
        subprocess.check_call([exe, os.path.join(root, "assets", "n7", "audio", "sfx"), os.path.join(out, "sound", "catarena")])
    # sound handle 0 is sound/feedback/hit.wav (registered first by the engine) and every Quake III
    # sound that is not shipped gets handle 0: a silent file there keeps them all silent
    os.makedirs(os.path.join(out, "sound", "feedback"), exist_ok=True)
    with open(os.path.join(out, "sound", "feedback", "hit.wav"), "wb") as f:
        frames = 441
        f.write(b"RIFF" + struct.pack("<I", 36 + 2 * frames) + b"WAVEfmt " + struct.pack("<IHHIIHH", 16, 1, 1, 44100, 88200, 2, 16)
                + b"data" + struct.pack("<I", 2 * frames) + bytes(2 * frames))
    os.makedirs(os.path.join(out, "music"), exist_ok=True)
    subprocess.check_call(["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-i", os.path.join(root, "assets", "n7", "audio", "main-theme.mp3"),
                           "-map_metadata", "-1", "-c:a", "libvorbis", "-q:a", "4", "-ar", "44100", "-ac", "2",
                           "-fflags", "+bitexact", "-flags:a", "+bitexact", os.path.join(out, "music", "ashen_drive.ogg")])
    print("soundtrack: music/ashen_drive.ogg (%d bytes)" % os.path.getsize(os.path.join(out, "music", "ashen_drive.ogg")))


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else ".")
