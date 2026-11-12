#!/usr/bin/env python3
"""Read-only reference inventory. This does not evaluate JS or bake gameplay data."""
import argparse, hashlib, json, pathlib, re, zipfile

def digest(data): return hashlib.sha256(data).hexdigest()
def inventory(root):
    files=[]
    for folder in ('legacy','reference'):
        for path in sorted((root/folder).rglob('*')):
            if not path.is_file(): continue
            data=path.read_bytes()
            row={'path':str(path.relative_to(root)), 'bytes':len(data),'sha256':digest(data)}
            if path.suffix.lower() in ('.zip','.apk'):
                with zipfile.ZipFile(path) as archive:
                    bad=archive.testzip()
                    if bad: raise ValueError(f'CRC failure: {path}: {bad}')
                    row['members']=[{'path':i.filename,'bytes':i.file_size,'sha256':digest(archive.read(i))}
                                    for i in archive.infolist() if not i.is_dir()]
            files.append(row)
    html=(root/'legacy/index.html').read_bytes()
    blocks=re.findall(rb'<script\b[^>]*>(.*?)</script\s*>',html,flags=re.S|re.I)
    return {'schema':1,'files':files,'primary_script_count':len(blocks),
            'primary_script_hashes':[digest(b) for b in blocks]}

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--root',type=pathlib.Path,default=pathlib.Path(__file__).resolve().parents[2])
    parser.add_argument('--output',type=pathlib.Path);parser.add_argument('--verify',type=pathlib.Path)
    args=parser.parse_args(); result=inventory(args.root)
    if args.verify:
        expected=json.loads(args.verify.read_text())
        if expected!=result: raise SystemExit('FAIL: source inventory differs')
        print(f"PASS: {len(result['files'])} reference files; {result['primary_script_count']} scripts")
    elif args.output: args.output.write_text(json.dumps(result,indent=2)+'\n')
    else: print(json.dumps(result,indent=2))
if __name__=='__main__': main()
