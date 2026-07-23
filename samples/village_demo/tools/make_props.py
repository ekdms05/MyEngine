#!/usr/bin/env python3
# make_props.py — village_demo 3D prop .glb generators (statue + fountain).
#
# Reuses the standard glTF 2.0 binary layout from tools/gen_cube_glb.py (right-handed, +Y up,
# -Z forward; MeshImporter converts to the engine's left-handed space on import). Each prop is a
# simple box-composite so it reads as a 3D object when the HybridRenderer sorts it against the
# 2.5D sprites (proves 3D props coexist with pixel characters).
#
#   statue.glb   — a tall narrow plinth + pillar (village monument). ~1x2 units.
#   fountain.glb — a low wide basin (village square fountain). ~2x0.6 units.
#
# Usage: python make_props.py [out_dir]
import struct, json, sys, os

def box(cx, cy, cz, hx, hy, hz):
    """Axis-aligned box centered at (cx,cy,cz) with half-extents (hx,hy,hz).
    Returns (positions, normals, texcoords, indices) with per-face verts (CCW from outside)."""
    faces = [
        ((0, 0, 1),  [(-1, -1, 1), (1, -1, 1), (1, 1, 1), (-1, 1, 1)]),    # +Z
        ((0, 0, -1), [(1, -1, -1), (-1, -1, -1), (-1, 1, -1), (1, 1, -1)]), # -Z
        ((1, 0, 0),  [(1, -1, 1), (1, -1, -1), (1, 1, -1), (1, 1, 1)]),     # +X
        ((-1, 0, 0), [(-1, -1, -1), (-1, -1, 1), (-1, 1, 1), (-1, 1, -1)]), # -X
        ((0, 1, 0),  [(-1, 1, 1), (1, 1, 1), (1, 1, -1), (-1, 1, -1)]),     # +Y
        ((0, -1, 0), [(-1, -1, -1), (1, -1, -1), (1, -1, 1), (-1, -1, 1)]), # -Y
    ]
    uv = [(0, 0), (1, 0), (1, 1), (0, 1)]
    P, N, T, I = [], [], [], []
    for n, corners in faces:
        base = len(P)
        for i, c in enumerate(corners):
            P.append((cx + c[0] * hx, cy + c[1] * hy, cz + c[2] * hz))
            N.append(n)
            T.append(uv[i])
        I += [base, base + 1, base + 2, base, base + 2, base + 3]
    return P, N, T, I

def merge(parts):
    P, N, T, I = [], [], [], []
    for (p, n, t, i) in parts:
        off = len(P)
        P += p; N += n; T += t
        I += [x + off for x in i]
    return P, N, T, I

def build_statue():
    # Plinth base (wide short) + tall pillar on top. Origin at ground (y=0), grows upward.
    base = box(0, 0.15, 0, 0.55, 0.15, 0.55)   # base slab y in [0,0.3]
    pillar = box(0, 1.05, 0, 0.28, 0.75, 0.28)  # pillar y in [0.3,1.8]
    cap = box(0, 1.9, 0, 0.4, 0.1, 0.4)         # cap y in [1.8,2.0]
    return merge([base, pillar, cap])

def build_fountain():
    # Low wide octagon-ish basin approximated by a wide short box + a small center spout.
    basin = box(0, 0.2, 0, 0.95, 0.2, 0.95)     # basin y in [0,0.4]
    rim   = box(0, 0.42, 0, 0.95, 0.05, 0.95)   # rim lip y in [0.37,0.47]
    spout = box(0, 0.62, 0, 0.14, 0.25, 0.14)   # center spout y in [0.37,0.87]
    return merge([basin, rim, spout])

def f32(v):
    return struct.pack('<%df' % len(v), *v)

def build_glb(path, mesh_fn, generator):
    pos, nrm, tc, idx = mesh_fn()
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

    xs = [p[0] for p in pos]; ys = [p[1] for p in pos]; zs = [p[2] for p in pos]
    vc = len(pos); ic = len(idx)
    gltf = {
        "asset": {"version": "2.0", "generator": generator},
        "buffers": [{"byteLength": len(bin_data)}],
        "bufferViews": [
            {"buffer": 0, "byteOffset": pos_off, "byteLength": vc * 12, "target": 34962},
            {"buffer": 0, "byteOffset": nrm_off, "byteLength": vc * 12, "target": 34962},
            {"buffer": 0, "byteOffset": uv_off,  "byteLength": vc * 8,  "target": 34962},
            {"buffer": 0, "byteOffset": idx_off, "byteLength": ic * 2,  "target": 34963},
        ],
        "accessors": [
            {"bufferView": 0, "componentType": 5126, "count": vc, "type": "VEC3",
             "min": [min(xs), min(ys), min(zs)], "max": [max(xs), max(ys), max(zs)]},
            {"bufferView": 1, "componentType": 5126, "count": vc, "type": "VEC3"},
            {"bufferView": 2, "componentType": 5126, "count": vc, "type": "VEC2"},
            {"bufferView": 3, "componentType": 5123, "count": ic, "type": "SCALAR"},
        ],
        "meshes": [{"primitives": [{"attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2},
                                    "indices": 3, "mode": 4}]}],
        "nodes": [{"mesh": 0}], "scenes": [{"nodes": [0]}], "scene": 0,
    }
    json_bytes = json.dumps(gltf, separators=(',', ':')).encode('utf-8')
    while len(json_bytes) % 4: json_bytes += b' '
    total = 12 + 8 + len(json_bytes) + 8 + len(bin_data)
    out = bytearray()
    out += b'glTF' + struct.pack('<II', 2, total)
    out += struct.pack('<I', len(json_bytes)) + b'JSON' + json_bytes
    out += struct.pack('<I', len(bin_data)) + b'BIN\x00' + bin_data
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'wb') as fp:
        fp.write(out)
    print("wrote %s (%d bytes, %d verts, %d indices)" % (path, len(out), vc, ic))

if __name__ == '__main__':
    out_dir = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
        os.path.dirname(__file__), '..', 'assets', 'mesh')
    out_dir = os.path.abspath(out_dir)
    build_glb(os.path.join(out_dir, 'statue.glb'),   build_statue,   'MyEngine make_props statue')
    build_glb(os.path.join(out_dir, 'fountain.glb'), build_fountain, 'MyEngine make_props fountain')
    print("DONE props")
