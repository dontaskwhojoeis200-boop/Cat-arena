#!/usr/bin/env python3
"""N7: extracts the legacy game's audio and menu art into assets/n7 (bytes unchanged) with provenance.

  python3 native/tools/n7/extract_n7_assets.py legacy/index.html assets/n7

Sources inside legacy/index.html (v0.17, SHA-256 checked):
  script 4  line 824   VOID_V318_SAMPLE_BANK   48 Ogg Vorbis samples (weapons, feline voices, explosion, body)
  script 1  line 809   VOID_EMBEDDED_ASSETS    assets/audio/main-theme.mp3 (soundtrack "ASHEN DRIVE")
  style     line 16713 --feral-bg              WebP menu background grain (converted to PNG with ffmpeg;
                                               the WebP bytes are kept too)
Every payload is written byte for byte as embedded; only the WebP is additionally converted.
"""
import base64, hashlib, json, os, re, subprocess, sys

LEGACY_SHA256 = 'e3c914bc9c1e216616a2bca125066eb2f3a660d4db1b76574c439df0d3510393'


def sha(b):
    return hashlib.sha256(b).hexdigest()


def main(html_path, out_dir):
    raw = open(html_path, 'rb').read()
    if sha(raw) != LEGACY_SHA256:
        sys.exit('legacy/index.html does not have the expected SHA-256 (v0.17)')
    lines = raw.decode('utf-8').split('\n')
    files = []

    def write(rel, data, source):
        path = os.path.join(out_dir, rel)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        open(path, 'wb').write(data)
        files.append({'path': rel, 'bytes': len(data), 'sha256': sha(data), 'source': source})

    # 1. sample bank
    line = lines[823]
    assert line.startswith('const VOID_V318_SAMPLE_BANK=')
    bank = json.loads(line[line.index('{'):line.rindex('}') + 1])
    index = []
    for key, b64 in bank.items():
        data = base64.b64decode(b64)
        assert data[:4] == b'OggS', key
        name = 'audio/sfx/' + key.replace(':', '_') + '.ogg'
        write(name, data, f'index.html line 824 VOID_V318_SAMPLE_BANK["{key}"] (base64)')
        index.append(f'{key}\t{os.path.basename(name)}')
    write('audio/sfx/bank.txt', ('\n'.join(index) + '\n').encode(), 'key -> file in VOID_V318_SAMPLE_BANK order (written by this tool; tools/n7/set_browser_lengths.py adds a third column)')

    # 2. embedded soundtrack
    line = lines[808]
    assert line.startswith('window.VOID_EMBEDDED_ASSETS=')
    assets = json.loads(line[line.index('{'):line.rindex('}') + 1])
    theme = assets['assets/audio/main-theme.mp3']
    assert theme['type'] == 'audio/mpeg'
    write('audio/main-theme.mp3', base64.b64decode(theme['data']), 'index.html line 809 VOID_EMBEDDED_ASSETS["assets/audio/main-theme.mp3"] (base64)')

    # 3. menu background grain
    line = lines[16712]
    m = re.search(r'--feral-bg:url\("data:image/webp;base64,([A-Za-z0-9+/=]+)"\)', line)
    assert m, 'menu background not found'
    webp = base64.b64decode(m.group(1))
    write('ui/feral-bg.webp', webp, 'index.html line 16713 CSS --feral-bg (base64 WebP)')
    png = os.path.join(out_dir, 'ui/feral-bg.png')
    subprocess.run(['ffmpeg', '-v', 'error', '-y', '-i', os.path.join(out_dir, 'ui/feral-bg.webp'), '-frames:v', '1', png], check=True)
    files.append({'path': 'ui/feral-bg.png', 'bytes': os.path.getsize(png), 'sha256': sha(open(png, 'rb').read()),
                  'source': 'ui/feral-bg.webp decoded with ffmpeg (lossless PNG of the decoded pixels)'})

    manifest = {
        'milestone': 'N7', 'legacy': 'legacy/index.html', 'legacySha256': LEGACY_SHA256,
        'provenance': [
            'All files come from the owner-supplied legacy game (v0.17, identifier VA-MODULAR-RB-011); none was downloaded or generated.',
            'Sample bank: legacy comment (line 11647 area) "v3.18: offline-rendered original sampled audio assets. No external URLs."',
            'Soundtrack: legacy comment "v3.10.3: An actual produced alt-metal soundtrack replaces the old WebAudio sequencer"; '
            'settings label "Play ASHEN DRIVE · original instrumental theme".',
            'Rights: stated as original by the legacy source; not independently verified by this tool. Redistribution decisions stay with the owner.',
            'Not imported: the unmerged v0.18 "SOUND BREAK" branch soundtrack (reference/alternate_branches), see docs/N7_UI_AUDIO.md.'],
        'files': files}
    open(os.path.join(out_dir, 'N7_ASSET_MANIFEST.json'), 'w').write(json.dumps(manifest, indent=1) + '\n')
    print(len(files), 'files')


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
