#!/usr/bin/env python3
"""Compares the frames written by run_path_matrix.sh with the N2 golden frames.

  compare_golden.py <golden-dir> <matrix-out-dir> <summary.json> <summary.md>

A configuration passes when every pose PNG decodes to exactly the golden pixels, its report shows
0 validation errors, and (sim_vk11_rejected) the run failed with the "No Vulkan 1.2" message.
Needs Pillow. Development/test tool only.
"""
import glob, hashlib, json, os, sys
from PIL import Image

def main():
    golden_dir, out_dir, json_path, md_path = sys.argv[1:5]
    golden = {os.path.basename(p): Image.open(p).convert('RGBA').tobytes() for p in sorted(glob.glob(os.path.join(golden_dir, '*.png')))}
    rows, ok = [], True
    for cfg in sorted(d for d in os.listdir(out_dir) if os.path.isdir(os.path.join(out_dir, d))):
        base = os.path.join(out_dir, cfg)
        log = open(os.path.join(base, 'run.log'), errors='replace').read() if os.path.exists(os.path.join(base, 'run.log')) else ''
        row = {'config': cfg}
        if cfg.endswith('_rejected'):
            row['rejected'] = 'No Vulkan 1.2' in log and not os.path.exists(os.path.join(base, 'report.json'))
            row['pass'] = row['rejected']
            row['message'] = next((l.split('message="')[1].rstrip('"') for l in log.splitlines() if 'No Vulkan 1.2' in l), '')
        else:
            rep = json.load(open(os.path.join(base, 'report.json')))
            v = rep['vulkan']
            same = sum(1 for name, pixels in golden.items()
                       if os.path.exists(os.path.join(base, name)) and Image.open(os.path.join(base, name)).convert('RGBA').tobytes() == pixels)
            row.update({
                'path': v['path'], 'deviceApi': v['deviceApi'], 'instanceApi': v['instanceApi'], 'negotiatedApi': v['negotiatedApi'],
                'features': {k: f['selected'] for k, f in v['features'].items()}, 'enabledExtensions': v['enabledExtensions'],
                'objects': v['objects'], 'framesIdenticalToN2Golden': same, 'frames': len(golden),
                'validationLayer': rep['validationLayer'], 'validationErrors': rep['validationErrors'], 'validationWarnings': rep['validationWarnings'],
                'drawCalls': [f['drawCalls'] for f in rep['frames']], 'materialPasses': [f['materialPasses'] for f in rep['frames']],
            })
            row['pass'] = same == len(golden) and rep['validationErrors'] == 0 and rep['validationLayer']
        ok = ok and row['pass']
        rows.append(row)
    json.dump({'allPass': ok, 'goldenFrames': sorted(golden), 'configurations': rows}, open(json_path, 'w'), indent=1)
    short = {'core': 'core', 'extension': 'ext', 'unavailable': '-'}
    with open(md_path, 'w') as md:
        md.write('| Configuration | Device API | Negotiated | DR | Sync2 | EDS | Push | HostCopy | Render passes / framebuffers | Material pipelines | Descriptor sets | Frames == N2 golden | Validation errors / warnings | Result |\n')
        md.write('|---|---|---|---|---|---|---|---|---|---|---|---|---|---|\n')
        for r in rows:
            if 'rejected' in r:
                md.write('| %s | 1.1 (simulated) | - | - | - | - | - | - | - | - | - | - | - | %s: device rejected |\n' % (r['config'], 'PASS' if r['pass'] else 'FAIL'))
                continue
            f, o = r['features'], r['objects']
            md.write('| %s | %s | %s | %s | %s | %s | %s | %s | %d / %d | %d | %d | %d / %d | %d / %d | %s |\n' % (
                r['config'], r['deviceApi'], r['negotiatedApi'], short[f['dynamic_rendering']], short[f['synchronization2']], short[f['extended_dynamic_state']],
                short[f['push_descriptor']], short[f['host_image_copy']], o['renderPasses'], o['framebuffers'], o['materialPipelines'], o['descriptorSets'],
                r['framesIdenticalToN2Golden'], r['frames'], r['validationErrors'], r['validationWarnings'], 'PASS' if r['pass'] else 'FAIL'))
    print('ALL PASS' if ok else 'FAILURES', '-', len(rows), 'configurations')
    sys.exit(0 if ok else 1)

main()
