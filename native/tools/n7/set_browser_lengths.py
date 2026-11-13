#!/usr/bin/env python3
"""Adds the decoded length the browser produces to assets/n7/audio/sfx/bank.txt (third column).

  python3 native/tools/n7/set_browser_lengths.py assets/n7 native/tests/fixtures/n7/decode.json

Chromium's decodeAudioData drops 11-192 frames (0.3-4 ms of near silence) at the end of these Ogg Vorbis
files that stb_vorbis returns (it trims by the last page's granule position only). The native loader
truncates every sample to the length measured in the browser (tools/n7/record_webaudio.cjs), so sample
durations, and with them the legacy voice-polyphony counting, match the legacy game.
"""
import json, os, sys
assets, decode = sys.argv[1], sys.argv[2]
frames = {e['key']: e['frames'] for e in json.load(open(decode))['decode']}
path = os.path.join(assets, 'audio/sfx/bank.txt')
rows = [l.split('\t')[:2] for l in open(path).read().split('\n') if l]
open(path, 'w').write(''.join(f'{k}\t{f}\t{frames[k]}\n' for k, f in rows))
import hashlib
manifest_path = os.path.join(assets, 'N7_ASSET_MANIFEST.json')
manifest = json.load(open(manifest_path))
data = open(path, 'rb').read()
for f in manifest['files']:
    if f['path'] == 'audio/sfx/bank.txt':
        f.update(bytes=len(data), sha256=hashlib.sha256(data).hexdigest(), source=f['source'] + '; third column = browser decode length (set_browser_lengths.py)')
open(manifest_path, 'w').write(json.dumps(manifest, indent=1) + '\n')
print(len(rows), 'lengths set')
