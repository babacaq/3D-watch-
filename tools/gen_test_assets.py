#!/usr/bin/env python3
"""Генерирует ТЕСТОВЫЕ файлы (не встроенные модели движка) для проверки импортёров:
test_skinned.glb (кости + анимация + текстура), test_cube.gltf (+.bin внешний), test_box.obj/.mtl/.png, test_tri.stl.
Использование: gen_test_assets.py <папка>"""
import json, math, os, struct, sys, zlib


def png(w, h, fn):
    raw = b''.join(b'\x00' + b''.join(bytes(fn(x, y)) for x in range(w)) for y in range(h))
    def chunk(t, d):
        c = struct.pack('>I', len(d)) + t + d
        return c + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    return b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(raw)) + chunk(b'IEND', b'')


def checker(x, y):
    return (230, 190, 60) if ((x // 8) + (y // 8)) % 2 == 0 else (60, 90, 200)


def box_geom(x0, x1, y0, y1, z0, z1):
    """позиции, нормали, uv, индексы коробки"""
    P, N, U, I = [], [], [], []
    faces = [((0, 0, 1), [(x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)]),
             ((0, 0, -1), [(x1, y0, z0), (x0, y0, z0), (x0, y1, z0), (x1, y1, z0)]),
             ((1, 0, 0), [(x1, y0, z1), (x1, y0, z0), (x1, y1, z0), (x1, y1, z1)]),
             ((-1, 0, 0), [(x0, y0, z0), (x0, y0, z1), (x0, y1, z1), (x0, y1, z0)]),
             ((0, 1, 0), [(x0, y1, z1), (x1, y1, z1), (x1, y1, z0), (x0, y1, z0)]),
             ((0, -1, 0), [(x0, y0, z0), (x1, y0, z0), (x1, y0, z1), (x0, y0, z1)])]
    for n, q in faces:
        b = len(P)
        P += q; N += [n] * 4; U += [(0, 1), (1, 1), (1, 0), (0, 0)]
        I += [b, b + 1, b + 2, b, b + 2, b + 3]
    return P, N, U, I


def pad4(b, c=b'\x00'):
    return b + c * ((4 - len(b) % 4) % 4)


def make_skinned_glb(path):
    # две кости: нижняя (узел 1) и верхняя (узел 2, ребёнок 1). Столб высотой 2: низ y 0..1, верх y 1..2
    P1, N1, U1, I1 = box_geom(-0.2, 0.2, 0, 1, -0.2, 0.2)
    P2, N2, U2, I2 = box_geom(-0.15, 0.15, 1, 2, -0.15, 0.15)
    P = P1 + P2; N = N1 + N2; U = U1 + U2
    I = I1 + [i + len(P1) for i in I2]
    J = [(0, 0, 0, 0)] * len(P1) + [(1, 0, 0, 0)] * len(P2)
    W = [(1.0, 0, 0, 0)] * len(P)
    # ibm: bone0 at origin (identity), bone1 at (0,1,0) -> inverse translate(0,-1,0)
    ibm = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1,
           1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, -1, 0, 1]
    times = [0.0, 0.5, 1.0]
    def qz(a): return (0, 0, math.sin(a / 2), math.cos(a / 2))
    rots = [qz(0), qz(0.9), qz(0)]
    buf = bytearray(); views = []; acc = []
    def add(data, target=None, **a):
        off = len(buf); buf.extend(data)
        while len(buf) % 4: buf.append(0)
        v = {'buffer': 0, 'byteOffset': off, 'byteLength': len(data)}
        if target: v['target'] = target
        views.append(v)
        acc.append(dict(bufferView=len(views) - 1, **a))
        return len(acc) - 1
    flat = lambda L: [c for t in L for c in t]
    aP = add(struct.pack('<%df' % (3 * len(P)), *flat(P)), 34962, componentType=5126, count=len(P), type='VEC3',
             min=[min(p[i] for p in P) for i in range(3)], max=[max(p[i] for p in P) for i in range(3)])
    aN = add(struct.pack('<%df' % (3 * len(N)), *flat(N)), 34962, componentType=5126, count=len(N), type='VEC3')
    aU = add(struct.pack('<%df' % (2 * len(U)), *flat(U)), 34962, componentType=5126, count=len(U), type='VEC2')
    aJ = add(struct.pack('<%dB' % (4 * len(J)), *flat(J)), 34962, componentType=5121, count=len(J), type='VEC4')
    aW = add(struct.pack('<%df' % (4 * len(W)), *flat(W)), 34962, componentType=5126, count=len(W), type='VEC4')
    aI = add(struct.pack('<%dH' % len(I), *I), 34963, componentType=5123, count=len(I), type='SCALAR')
    aM = add(struct.pack('<32f', *ibm), None, componentType=5126, count=2, type='MAT4')
    aT = add(struct.pack('<3f', *times), None, componentType=5126, count=3, type='SCALAR', min=[0], max=[1])
    aR = add(struct.pack('<12f', *flat(rots)), None, componentType=5126, count=3, type='VEC4')
    img = png(32, 32, checker)
    imgOff = len(buf); buf.extend(img)
    while len(buf) % 4: buf.append(0)
    views.append({'buffer': 0, 'byteOffset': imgOff, 'byteLength': len(img)})
    g = {
        'asset': {'version': '2.0'}, 'scene': 0, 'scenes': [{'nodes': [0, 1]}],
        'nodes': [{'name': 'Mesh', 'mesh': 0, 'skin': 0}, {'name': 'Bone0', 'children': [2]},
                  {'name': 'Bone1', 'translation': [0, 1, 0]}],
        'meshes': [{'primitives': [{'attributes': {'POSITION': aP, 'NORMAL': aN, 'TEXCOORD_0': aU, 'JOINTS_0': aJ, 'WEIGHTS_0': aW},
                                    'indices': aI, 'material': 0}]}],
        'skins': [{'joints': [1, 2], 'inverseBindMatrices': aM, 'skeleton': 1}],
        'materials': [{'pbrMetallicRoughness': {'baseColorTexture': {'index': 0}, 'metallicFactor': 0, 'roughnessFactor': 0.6}}],
        'textures': [{'source': 0, 'sampler': 0}], 'samplers': [{'magFilter': 9729, 'minFilter': 9987}],
        'images': [{'bufferView': len(views) - 1, 'mimeType': 'image/png'}],
        'animations': [{'name': 'Wave', 'samplers': [{'input': aT, 'output': aR, 'interpolation': 'LINEAR'}],
                        'channels': [{'sampler': 0, 'target': {'node': 2, 'path': 'rotation'}}]}],
        'accessors': acc, 'bufferViews': views, 'buffers': [{'byteLength': len(buf)}]}
    js = pad4(json.dumps(g).encode(), b' ')
    bn = pad4(bytes(buf))
    out = struct.pack('<III', 0x46546C67, 2, 12 + 8 + len(js) + 8 + len(bn))
    out += struct.pack('<II', len(js), 0x4E4F534A) + js + struct.pack('<II', len(bn), 0x004E4942) + bn
    open(path, 'wb').write(out)


def make_cube_gltf(d):
    P, N, U, I = box_geom(-0.5, 0.5, 0, 1, -0.5, 0.5)
    flat = lambda L: [c for t in L for c in t]
    b = struct.pack('<%df' % (3 * len(P)), *flat(P)) + struct.pack('<%df' % (3 * len(N)), *flat(N)) + struct.pack('<%dH' % len(I), *I)
    open(os.path.join(d, 'test_cube.bin'), 'wb').write(b)
    g = {'asset': {'version': '2.0'}, 'scene': 0, 'scenes': [{'nodes': [0]}],
         'nodes': [{'mesh': 0, 'rotation': [0, 0.2588, 0, 0.9659]}],
         'meshes': [{'primitives': [{'attributes': {'POSITION': 0, 'NORMAL': 1}, 'indices': 2, 'material': 0}]}],
         'materials': [{'pbrMetallicRoughness': {'baseColorFactor': [0.8, 0.2, 0.2, 1]}}],
         'accessors': [{'bufferView': 0, 'componentType': 5126, 'count': len(P), 'type': 'VEC3', 'min': [-.5, 0, -.5], 'max': [.5, 1, .5]},
                       {'bufferView': 1, 'componentType': 5126, 'count': len(N), 'type': 'VEC3'},
                       {'bufferView': 2, 'componentType': 5123, 'count': len(I), 'type': 'SCALAR'}],
         'bufferViews': [{'buffer': 0, 'byteOffset': 0, 'byteLength': 12 * len(P)},
                         {'buffer': 0, 'byteOffset': 12 * len(P), 'byteLength': 12 * len(N)},
                         {'buffer': 0, 'byteOffset': 24 * len(P), 'byteLength': 2 * len(I)}],
         'buffers': [{'uri': 'test_cube.bin', 'byteLength': len(b)}]}
    json.dump(g, open(os.path.join(d, 'test_cube.gltf'), 'w'))


def make_obj(d):
    open(os.path.join(d, 'test_box.png'), 'wb').write(png(16, 16, checker))
    open(os.path.join(d, 'test_box.mtl'), 'w').write('newmtl mat\nKd 1 1 1\nKs 0.3 0.3 0.3\nNs 40\nmap_Kd test_box.png\n')
    P, N, U, I = box_geom(-1, 1, 0, 1.5, -0.6, 0.6)
    L = ['mtllib test_box.mtl', 'o Box']
    L += ['v %g %g %g' % p for p in P] + ['vt %g %g' % (u, 1 - v) for u, v in U] + ['vn %g %g %g' % n for n in N]
    L.append('usemtl mat')
    for k in range(0, len(I), 3):
        L.append('f ' + ' '.join('%d/%d/%d' % (i + 1, i + 1, i + 1) for i in I[k:k + 3]))
    open(os.path.join(d, 'test_box.obj'), 'w').write('\n'.join(L) + '\n')


def make_stl(d):
    tris = [((0, 0, 1), (0, 0, 0), (1, 0, 0), (0, 1, 0)), ((0, 0, 1), (1, 0, 0), (1, 1, 0), (0, 1, 0))]
    out = bytearray(80) + struct.pack('<I', len(tris))
    for n, a, b, c in tris:
        out += struct.pack('<12fH', *n, *a, *b, *c, 0)
    open(os.path.join(d, 'test_tri.stl'), 'wb').write(bytes(out))


if __name__ == '__main__':
    d = sys.argv[1] if len(sys.argv) > 1 else '.'
    os.makedirs(d, exist_ok=True)
    make_skinned_glb(os.path.join(d, 'test_skinned.glb'))
    make_cube_gltf(d); make_obj(d); make_stl(d)
    print('ok', sorted(os.listdir(d)))
