#!/usr/bin/env python3
"""One-command N1 bake. Requires Node22+, npm ci in this directory, Python3.11+.
No browser, GPU, network fetch or JS runtime is required by the resulting native data.
"""
import argparse, hashlib, json, pathlib, struct, subprocess, zipfile, zlib
ROOT=pathlib.Path(__file__).resolve().parents[2]
IDS=('helios-crucible','bastion-keep','reactor-bastion','q3-gallery')
def sha(data):return hashlib.sha256(data).hexdigest()
def dump(path,value):path.parent.mkdir(parents=True,exist_ok=True);path.write_text(json.dumps(value,sort_keys=True,indent=2,ensure_ascii=False,allow_nan=False)+'\n')
def glb_json(data):
    magic,version,size=struct.unpack_from('<4sII',data)
    assert magic==b'glTF' and version==2 and size==len(data),'invalid GLB header'
    n,kind=struct.unpack_from('<I4s',data,12)
    assert kind==b'JSON' and 20+n<=len(data),'invalid GLB JSON chunk'
    return json.loads(data[20:20+n])
def value(v):
    if v is None:return b'\0'
    if isinstance(v,bool):return bytes([2 if v else 1])
    if isinstance(v,(float,int)):return b'\3'+struct.pack('<d',v)
    if isinstance(v,str):b=v.encode();return b'\4'+struct.pack('<I',len(b))+b
    if isinstance(v,list):return b'\5'+struct.pack('<I',len(v))+b''.join(map(value,v))
    return b'\6'+struct.pack('<I',len(v))+b''.join(value(k)+value(v[k]) for k in sorted(v))
def datafile(path,v):
    b=value(v);path.write_bytes(b'CATDATA1'+struct.pack('<III',1,len(b),zlib.crc32(b))+b)
def finish(out):
    catalog=json.loads((out/'catalog.json').read_text())
    character=ROOT/'reference/character/CAT_MARINE_MODEL_AND_SOURCE.zip'
    with zipfile.ZipFile(character) as z:
        for name in ('cat_marine_rigged.glb','cat_marine_hitboxes.json','model_metadata.json','armor_camo_256.png','cat_face_256.png'):
            p=out/'models/character'/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(z.read(name))
    data=(out/'models/character/cat_marine_rigged.glb').read_bytes();g=glb_json(data)
    catalog['character']={'file':'models/character/cat_marine_rigged.glb','sha256':sha(data),'sourceArchiveSha256':sha(character.read_bytes()),'hitboxes':'models/character/cat_marine_hitboxes.json',
       'joints':len(g['skins'][0]['joints']),'animations':[a.get('name','') for a in g['animations']],
       'axis':'glTF +Y up; hitbox metadata +Z forward; native world +Z up. Runtime animation/axis integration remains N6.',
       'thirdPersonOnly':True}
    assert catalog['character']['joints']==21 and len(g['animations'])==7
    # Standalone glTF for fitted static kit meshes. Preserve source buffers exactly;
    # these are extracted fitted assets, NOT fabricated copies of upstream source GLBs.
    for entry in catalog['models']:
        if entry['group']!='kit':continue
        meta=json.loads((out/entry['metadata']).read_text());id=entry['id'];buffers=[];views=[];access=[]
        for key,components,type_,component in [('pos',3,'VEC3',5126),('nrm',3,'VEC3',5126),('uv',2,'VEC2',5126),('idx',1,'SCALAR',5123)]:
            p=out/meta[key]['file'];b=p.read_bytes();size=2 if key=='idx' else 4
            assert len(b)%(size*components)==0
            buffers.append({'uri':p.name,'byteLength':len(b)})
            views.append({'buffer':len(buffers)-1,'byteLength':len(b),'target':34963 if key=='idx' else 34962})
            a={'bufferView':len(views)-1,'componentType':component,'count':len(b)//(size*components),'type':type_}
            if key=='pos':
                f=struct.unpack('<'+'f'*(len(b)//4),b);a['min']=[min(f[j::3]) for j in range(3)];a['max']=[max(f[j::3]) for j in range(3)]
            access.append(a)
        t=next(t for t in catalog['textures'] if t['id']==id)
        gltf={'asset':{'version':'2.0','generator':'Cat Arena N1 fitted-source extractor'},'buffers':buffers,'bufferViews':views,'accessors':access,
          'images':[{'uri':'../../'+t['file']}], 'textures':[{'source':0}],
          'materials':[{'pbrMetallicRoughness':{'baseColorTexture':{'index':0},'metallicFactor':0,'roughnessFactor':1},'doubleSided':True}],
          'meshes':[{'primitives':[{'attributes':{'POSITION':0,'NORMAL':1,'TEXCOORD_0':2},'indices':3,'material':0}]}],
          # Proper rotation: world z-up -> glTF y-up, preserving right-handedness.
          'nodes':[{'mesh':0,'rotation':[-0.7071067811865476,0,0,0.7071067811865476]}], 'scenes':[{'nodes':[0]}],'scene':0,
          'extras':{'sourceAssetId':id,'sourceModelSHA256':meta['sha256'],'fittedGameSpaceBuffers':True,'upstreamOriginalGLBIncluded':False,
                    'appearanceNote':'Diffuse inspection export; native shader stage definitions remain authoritative for emissive/vertex light behavior.'}}
        file=f'models/kit/{id}.gltf';dump(out/file,gltf);entry['gltf']=file
    catalog['sourceAuthority']={'primary':'legacy/index.html','sha256':sha((ROOT/'legacy/index.html').read_bytes()),'soundtrackBranchMerged':False}
    dump(out/'catalog.json',catalog);datafile(out/'catalog.cadata',catalog)
    summary=[]
    for id in IDS:
        m=json.loads((out/f'maps/{id}/map.json').read_text());c=m['compiled'];r=m['render']
        row={'id':id,'slabs':len(c['slabs']),'ramps':len(m['level']['ramps']),'walls':len(c['walls']),'solids':len(c['solids']),'cavities':len(c['cavities']),
          'spawns':len(c['spawns']),'pickups':len(c['pickups']),'jumpPads':len(c['pads']),'kitPlacements':len(m['authored']['kits']),
          'worldTriangles':r['world']['triangles'],'materialTriangles':r['materialMesh']['triangles'],'skyTriangles':r['sky']['triangles'],
          'lightmapSourceHash':m['lightmap']['hash'],'lightmapComputedHash':m['lightmap']['computedHash'],'lightmapMatched':m['lightmap']['compatible']}
        summary.append(row)
        preview(out,id,m)
    inventory={'schema':1,'maps':summary,'modelCounts':{k:sum(e['group']==k for e in catalog['models']) for k in ('enemies','weapons','kit')},
      'character':catalog['character'],'proceduralAndKitTextures':len(catalog['textures']),'sfx':len(catalog['audio']),
      'lightmaps':len(catalog['lightmaps']),'visibilitySets':len(catalog['visibility']),
      'limitations':['Native renderer does not consume these maps until N2.','Runtime A* graph is not baked; authored connections, walkable surfaces and spatial indices are exported.',
      'Native material shaders, animation and audio playback not implemented in N1.','Canvas rasterization was performed by pinned Skia, not browser pixel-parity tested.',
      'Legacy fallback vertex lighting is not recomputed. Matched shipped atlas/UVs provide the preserved static lightmap path.']}
    dump(out/'inventory.json',inventory)
    # Final manifest covers all output bytes except itself; no wall-clock timestamps.
    old=json.loads((out/'conversion-manifest.json').read_text())
    old['files']=[{'path':str(p.relative_to(out)),'bytes':p.stat().st_size,'sha256':sha(p.read_bytes())} for p in sorted(out.rglob('*')) if p.is_file() and p.name!='conversion-manifest.json']
    old['characterArchiveSha256']=sha(character.read_bytes());old['schema']=1
    dump(out/'conversion-manifest.json',old)
    print(json.dumps(inventory,indent=2))
def preview(out,id,m):
    # Exact schematic from exported polygons, not a simulated gameplay screenshot.
    colors=['#51bac5','#d1d5db','#dc9b51','#b78fe8'];parts=['<svg xmlns="http://www.w3.org/2000/svg" width="900" height="850" viewBox="0 0 900 850">',
      '<rect width="900" height="850" fill="#11151c"/>',f'<text x="28" y="35" fill="white" font-size="24">{id} — exported geometry plan</text>',
      '<text x="28" y="60" fill="#b8c0cc" font-size="14">Z-up layered slabs • schematic only • no native rendering claim</text>']
    for slab in sorted(m['authored']['slabs'],key=lambda s:s['z']):
        z=slab['z'];color=colors[0 if z<0 else 1 if z<4 else 2 if z<9 else 3]
        points=' '.join(f'{30+p[0]*6.5:.2f},{815-p[1]*6.0:.2f}' for p in slab['poly'])
        parts.append(f'<polygon points="{points}" fill="{color}" fill-opacity="0.16" stroke="{color}" stroke-width="0.7"/>')
    for s in m['compiled']['spawns']:parts.append(f'<circle cx="{30+s["x"]*6.5:.2f}" cy="{815-s["y"]*6.0:.2f}" r="3" fill="#96ef9b"/>')
    for i,(label,c) in enumerate(zip(['z&lt;0','0≤z&lt;4','4≤z&lt;9','z≥9'],colors)):
        parts.append(f'<text x="{30+i*170}" y="838" fill="{c}" font-size="15">{label}</text>')
    parts.append('</svg>');p=out/f'previews/{id}.svg';p.parent.mkdir(parents=True,exist_ok=True);p.write_text('\n'.join(parts)+'\n')
def main():
    a=argparse.ArgumentParser();a.add_argument('--out',type=pathlib.Path,default=ROOT/'assets/n1');a.add_argument('--finish-existing',action='store_true',help='Development recovery: finish an already successful JS bake');args=a.parse_args();out=args.out.resolve()
    if not args.finish_existing:subprocess.run(['node',str(ROOT/'native/tools/bake.cjs'),str(out)],check=True)
    else:assert (out/'conversion-manifest.json').exists(),'JS bake did not finish'
    finish(out)
if __name__=='__main__':main()
