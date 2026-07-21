#!/usr/bin/env python3
# gen_cube_glb.py — MeshImporter(04) 통합 데모용 큐브/석상 대용 .glb 생성기.
#
# glTF 2.0 규약(오른손·+Y업·-Z전방)으로 정직하게 기록한다. MeshImporter가 임포트 시
# 왼손 좌표로 변환(z 반전 + 와인딩 뒤집기)하므로, 여기서는 표준 glTF 그대로 둔다.
# 산출: 24정점(면당 4)·36인덱스, POSITION/NORMAL/TEXCOORD_0, UNSIGNED_SHORT 인덱스.
import struct, json, sys, os

def build_cube(half=1.0):
    faces = [
        # (normal, [4 corners CCW as seen from outside])
        ((0,0,1),  [(-1,-1,1),(1,-1,1),(1,1,1),(-1,1,1)]),   # +Z
        ((0,0,-1), [(1,-1,-1),(-1,-1,-1),(-1,1,-1),(1,1,-1)]),# -Z
        ((1,0,0),  [(1,-1,1),(1,-1,-1),(1,1,-1),(1,1,1)]),   # +X
        ((-1,0,0), [(-1,-1,-1),(-1,-1,1),(-1,1,1),(-1,1,-1)]),# -X
        ((0,1,0),  [(-1,1,1),(1,1,1),(1,1,-1),(-1,1,-1)]),   # +Y
        ((0,-1,0), [(-1,-1,-1),(1,-1,-1),(1,-1,1),(-1,-1,1)]),# -Y
    ]
    uv = [(0,0),(1,0),(1,1),(0,1)]
    positions, normals, texcoords, indices = [], [], [], []
    for n, corners in faces:
        base = len(positions)
        for i,c in enumerate(corners):
            positions.append((c[0]*half, c[1]*half, c[2]*half))
            normals.append(n)
            texcoords.append(uv[i])
        indices += [base, base+1, base+2, base, base+2, base+3]
    return positions, normals, texcoords, indices

def f32(v): return struct.pack('<%df' % len(v), *v)

def build_glb(path):
    pos, nrm, tc, idx = build_cube(1.0)
    bin_data = bytearray()
    pos_off = len(bin_data)
    for p in pos: bin_data += f32(p)
    nrm_off = len(bin_data)
    for n in nrm: bin_data += f32(n)
    uv_off = len(bin_data)
    for t in tc: bin_data += f32(t)
    while len(bin_data) % 4: bin_data.append(0)
    idx_off = len(bin_data)
    bin_data += struct.pack('<%dH' % len(idx), *idx)
    while len(bin_data) % 4: bin_data.append(0)

    xs=[p[0] for p in pos]; ys=[p[1] for p in pos]; zs=[p[2] for p in pos]
    vc=len(pos); ic=len(idx)
    gltf = {
        "asset":{"version":"2.0","generator":"MyEngine gen_cube_glb"},
        "buffers":[{"byteLength":len(bin_data)}],
        "bufferViews":[
            {"buffer":0,"byteOffset":pos_off,"byteLength":vc*12,"target":34962},
            {"buffer":0,"byteOffset":nrm_off,"byteLength":vc*12,"target":34962},
            {"buffer":0,"byteOffset":uv_off,"byteLength":vc*8,"target":34962},
            {"buffer":0,"byteOffset":idx_off,"byteLength":ic*2,"target":34963},
        ],
        "accessors":[
            {"bufferView":0,"componentType":5126,"count":vc,"type":"VEC3",
             "min":[min(xs),min(ys),min(zs)],"max":[max(xs),max(ys),max(zs)]},
            {"bufferView":1,"componentType":5126,"count":vc,"type":"VEC3"},
            {"bufferView":2,"componentType":5126,"count":vc,"type":"VEC2"},
            {"bufferView":3,"componentType":5123,"count":ic,"type":"SCALAR"},
        ],
        "meshes":[{"primitives":[{"attributes":{"POSITION":0,"NORMAL":1,"TEXCOORD_0":2},"indices":3,"mode":4}]}],
        "nodes":[{"mesh":0}],"scenes":[{"nodes":[0]}],"scene":0,
    }
    json_bytes = json.dumps(gltf, separators=(',',':')).encode('utf-8')
    while len(json_bytes) % 4: json_bytes += b' '

    total = 12 + 8 + len(json_bytes) + 8 + len(bin_data)
    out = bytearray()
    out += b'glTF' + struct.pack('<II', 2, total)
    out += struct.pack('<I', len(json_bytes)) + b'JSON' + json_bytes
    out += struct.pack('<I', len(bin_data)) + b'BIN\x00' + bin_data
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path,'wb') as fp: fp.write(out)
    print("wrote %s (%d bytes, %d verts, %d indices)" % (path, len(out), vc, ic))

if __name__ == '__main__':
    dst = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
        os.path.dirname(__file__), '..', 'samples', 'assets', 'mesh', 'cube.glb')
    build_glb(os.path.abspath(dst))
