#!/usr/bin/env python3
"""Independent standard-library validation of the JS/Python baker's outputs."""
import argparse,hashlib,json,math,pathlib,struct,zlib
from bake_assets import IDS,glb_json

def decode_data(b):
    assert b[:8]==b'CATDATA1'
    version,n,crc=struct.unpack_from('<III',b,8);assert version==1 and len(b)==20+n and zlib.crc32(b[20:])==crc
    offset=20
    def take(n):
        nonlocal offset
        assert 0<=n<=len(b)-offset
        r=b[offset:offset+n];offset+=n;return r
    def u32():return struct.unpack('<I',take(4))[0]
    def value(depth=0):
        assert depth<=64
        tag=take(1)[0]
        if tag==0:return None
        if tag==1:return False
        if tag==2:return True
        if tag==3:
            n=struct.unpack('<d',take(8))[0];assert math.isfinite(n);return n
        if tag==4:return take(u32()).decode('utf8')
        if tag==5:return [value(depth+1) for _ in range(u32())]
        if tag==6:
            result={}
            for _ in range(u32()):
                k=value(depth+1);assert isinstance(k,str) and k not in result;result[k]=value(depth+1)
            return result
        raise AssertionError('unknown value tag')
    result=value();assert offset==len(b);return result

def mesh(b):
    assert b[:8]==b'CATMESH1'
    version,stride,nv,ni,crc=struct.unpack_from('<IIIII',b,8)
    assert version==1 and stride in (12,14,18) and ni%3==0 and len(b)==28+nv*stride*4+ni*4 and zlib.crc32(b[28:])==crc
    floats=struct.unpack_from('<'+'f'*(nv*stride),b,28);assert all(map(math.isfinite,floats))
    indices=struct.unpack_from('<'+'I'*ni,b,28+nv*stride*4);assert all(i<nv for i in indices)
    return stride,nv,ni

def validate(root):
    manifest=json.loads((root/'conversion-manifest.json').read_text());paths=set()
    for entry in manifest['files']:
        p=(root/entry['path']).resolve();assert p.is_relative_to(root.resolve()) and entry['path'] not in paths;paths.add(entry['path'])
        b=p.read_bytes();assert len(b)==entry['bytes'] and hashlib.sha256(b).hexdigest()==entry['sha256'],p
    assert paths=={str(p.relative_to(root)) for p in root.rglob('*') if p.is_file() and p.name!='conversion-manifest.json'},'untracked or missing output'
    for p in root.rglob('*.cmesh'):mesh(p.read_bytes())
    for id in IDS:
        m=json.loads((root/f'maps/{id}/map.json').read_text());assert decode_data((root/f'maps/{id}/map.cadata').read_bytes())==m
        assert m['id']==id and m['wireId']==id+'-v1'
        assert m['lightmap']['compatible'] and m['lightmap']['hash']==m['lightmap']['computedHash'],'shipped atlas mismatch'
        png=(root/m['lightmap']['png']).read_bytes();assert png[:8]==b'\x89PNG\r\n\x1a\n'
        assert struct.unpack_from('>II',png,16)==(m['lightmap']['computedWidth'],m['lightmap']['computedHeight'])
        for key in ('world','sky','materialMesh'):
            desc=m['render'][key];stride,nv,ni=mesh((root/desc['file']).read_bytes());assert (stride,nv,ni)==(desc['stride'],desc['vertices'],desc['indices'])
        # Multiple independent heights, ramps, authored collision/visual distinction.
        assert len({s['z'] for s in m['compiled']['slabs']})>=3
        def inside(x,y,poly):
            hit=False
            for a,b in zip(poly,poly[1:]+poly[:1]):
                if (a[1]>y)!=(b[1]>y) and x<(b[0]-a[0])*(y-a[1])/(b[1]-a[1])+a[0]:hit=not hit
            return hit
        flat=[s for s in m['compiled']['slabs'] if s['nav'] and s['plane'] is None]
        stacked=False
        for slab in flat:
            x=sum(p[0] for p in slab['poly'])/len(slab['poly']);y=sum(p[1] for p in slab['poly'])/len(slab['poly'])
            if len({s['z'] for s in flat if inside(x,y,s['poly'])})>=2:stacked=True;break
        assert stacked,'stacked walkable surfaces lost'

        assert m['level']['ramps'] and m['compiled']['spawns'] and m['compiled']['walls']
        assert all(s['src']['$ref'].startswith('authored.') for s in m['compiled']['slabs'])
        names={x['name'] for x in m['materialDefinitions']}
        assert all(x['material'] in names for x in m['render']['materials'])
        assert m['navigation']['runtimeAStarGraphBaked'] is False
        print('PASS map',id,'source links, binary/JSON equality, render ranges, matched lightmap dimensions/hash')
    c=json.loads((root/'catalog.json').read_text());assert decode_data((root/'catalog.cadata').read_bytes())==c
    assert len([m for m in c['models'] if m['group']=='weapons'])==16
    assert len([m for m in c['models'] if m['group']=='enemies'])==20
    assert len(c['audio'])==48 and len(c['lightmaps'])==4
    for p in root.rglob('*.gltf'):
        g=json.loads(p.read_text());sizes=[]
        for b in g['buffers']:
            uri=(p.parent/b['uri']).resolve();assert uri.is_relative_to(root.resolve());assert uri.stat().st_size==b['byteLength'];sizes.append(b['byteLength'])
        for v in g['bufferViews']:assert v.get('byteOffset',0)+v['byteLength']<=sizes[v['buffer']]
        for a in g['accessors']:
            components={'SCALAR':1,'VEC2':2,'VEC3':3}[a['type']];size={5123:2,5126:4}[a['componentType']]
            assert a['count']*components*size<=g['bufferViews'][a['bufferView']]['byteLength']
        for im in g['images']:assert (p.parent/im['uri']).resolve().is_relative_to(root.resolve()) and (p.parent/im['uri']).is_file()
    if 'gameplay' in c:
        gameplay=json.loads((root/'gameplay/constants.json').read_text());assert decode_data((root/c['gameplay']).read_bytes())==gameplay
        assert len(gameplay['WEAPONS'])==16 and len(gameplay['ENEMY_TYPES'])==20 and gameplay['GRAVITY']==18
    char=glb_json((root/c['character']['file']).read_bytes());assert len(char['skins'][0]['joints'])==21 and len(char['animations'])==7
    hit=json.loads((root/c['character']['hitboxes']).read_text());assert hit['selfHitboxesEnabled'] is False
    print('PASS catalog: 16 weapons, 20 enemy meshes, 48 SFX, kit glTF buffer bounds, 21-joint/7-clip character/hitboxes')
    print('PASS manifest:',len(paths),'files; checksums, native containers, references, no stale outputs')
    return manifest

def main():
    p=argparse.ArgumentParser();p.add_argument('root',type=pathlib.Path);p.add_argument('--compare',type=pathlib.Path);a=p.parse_args();first=validate(a.root)
    if a.compare:
        other=validate(a.compare);assert first==other,'conversion outputs not reproducible';print('PASS two clean bakes: byte-identical manifest and every output hash')
if __name__=='__main__':main()
