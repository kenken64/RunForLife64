#!/usr/bin/env python3
import io
import json
import math
import struct
from pathlib import Path

from PIL import Image, ImageEnhance, ImageFilter


ROOT = Path(__file__).resolve().parents[1]
MODEL_DIR = ROOT / "assets" / "models"
OUT_DIR = ROOT / "assets" / "generated"
SPRITE_W = 64
SPRITE_H = 128
VIEW_YAW_DEG = 28.0
VIEW_PITCH_DEG = -8.0
MARGIN = 3
MAX_RENDER_TRIANGLES = 1200000

JOBS = [
    {
        "glob": "building*.glb",
        "suffix": "_billboard",
        "width": 64,
        "height": 128,
        "yaw": 28.0,
        "pitch": -8.0,
    },
    {
        "glob": "backtofuture.glb",
        "output": "backtofuture_player.png",
        "width": 96,
        "height": 96,
        "yaw": 34.0,
        "pitch": -10.0,
    },
]

COMPONENT_FORMATS = {
    5120: ("b", 1),
    5121: ("B", 1),
    5122: ("h", 2),
    5123: ("H", 2),
    5125: ("I", 4),
    5126: ("f", 4),
}

TYPE_COUNTS = {
    "SCALAR": 1,
    "VEC2": 2,
    "VEC3": 3,
    "VEC4": 4,
    "MAT2": 4,
    "MAT3": 9,
    "MAT4": 16,
}


def read_glb(path):
    data = path.read_bytes()
    magic, version, total_len = struct.unpack_from("<4sII", data, 0)
    if magic != b"glTF" or version != 2 or total_len != len(data):
        raise ValueError(f"{path} is not a valid GLB 2.0 file")

    gltf = None
    bin_chunk = None
    offset = 12
    while offset < len(data):
        chunk_len, chunk_type = struct.unpack_from("<II", data, offset)
        offset += 8
        chunk = data[offset:offset + chunk_len]
        offset += chunk_len
        if chunk_type == 0x4E4F534A:
            gltf = json.loads(chunk.decode("utf-8").rstrip("\x00 \t\r\n"))
        elif chunk_type == 0x004E4942:
            bin_chunk = chunk

    if gltf is None or bin_chunk is None:
        raise ValueError(f"{path} is missing JSON or BIN chunks")
    return gltf, bin_chunk


def read_accessor(gltf, bin_chunk, index):
    accessor = gltf["accessors"][index]
    view = gltf["bufferViews"][accessor["bufferView"]]
    fmt_char, comp_size = COMPONENT_FORMATS[accessor["componentType"]]
    elem_count = TYPE_COUNTS[accessor["type"]]
    count = accessor["count"]
    stride = view.get("byteStride", comp_size * elem_count)
    offset = view.get("byteOffset", 0) + accessor.get("byteOffset", 0)
    fmt = "<" + fmt_char * elem_count

    values = []
    for i in range(count):
        item = struct.unpack_from(fmt, bin_chunk, offset + i * stride)
        values.append(item[0] if elem_count == 1 else item)
    return values


def identity_matrix():
    return [
        1.0, 0.0, 0.0, 0.0,
        0.0, 1.0, 0.0, 0.0,
        0.0, 0.0, 1.0, 0.0,
        0.0, 0.0, 0.0, 1.0,
    ]


def matrix_from_gltf(values):
    return [float(values[col * 4 + row]) for row in range(4) for col in range(4)]


def multiply_matrix(a, b):
    out = [0.0] * 16
    for row in range(4):
        for col in range(4):
            out[row * 4 + col] = sum(a[row * 4 + k] * b[k * 4 + col] for k in range(4))
    return out


def trs_matrix(node):
    if "matrix" in node:
        return matrix_from_gltf(node["matrix"])

    tx, ty, tz = node.get("translation", [0.0, 0.0, 0.0])
    sx, sy, sz = node.get("scale", [1.0, 1.0, 1.0])
    qx, qy, qz, qw = node.get("rotation", [0.0, 0.0, 0.0, 1.0])

    xx = qx * qx
    yy = qy * qy
    zz = qz * qz
    xy = qx * qy
    xz = qx * qz
    yz = qy * qz
    wx = qw * qx
    wy = qw * qy
    wz = qw * qz

    return [
        (1.0 - 2.0 * (yy + zz)) * sx, (2.0 * (xy - wz)) * sy, (2.0 * (xz + wy)) * sz, tx,
        (2.0 * (xy + wz)) * sx, (1.0 - 2.0 * (xx + zz)) * sy, (2.0 * (yz - wx)) * sz, ty,
        (2.0 * (xz - wy)) * sx, (2.0 * (yz + wx)) * sy, (1.0 - 2.0 * (xx + yy)) * sz, tz,
        0.0, 0.0, 0.0, 1.0,
    ]


def transform_point(matrix, point):
    x, y, z = point
    return (
        matrix[0] * x + matrix[1] * y + matrix[2] * z + matrix[3],
        matrix[4] * x + matrix[5] * y + matrix[6] * z + matrix[7],
        matrix[8] * x + matrix[9] * y + matrix[10] * z + matrix[11],
    )


def collect_mesh_instances(gltf):
    nodes = gltf.get("nodes", [])
    scenes = gltf.get("scenes", [])
    if not nodes:
        return [(i, identity_matrix()) for i in range(len(gltf.get("meshes", [])))]

    roots = scenes[gltf.get("scene", 0)].get("nodes", []) if scenes else list(range(len(nodes)))
    instances = []

    def walk(node_index, parent):
        node = nodes[node_index]
        matrix = multiply_matrix(parent, trs_matrix(node))
        if "mesh" in node:
            instances.append((node["mesh"], matrix))
        for child in node.get("children", []):
            walk(child, matrix)

    for root in roots:
        walk(root, identity_matrix())
    return instances


def load_texture(gltf, bin_chunk):
    images = gltf.get("images", [])
    if not images:
        return None
    image = images[0]
    view = gltf["bufferViews"][image["bufferView"]]
    start = view.get("byteOffset", 0)
    blob = bin_chunk[start:start + view["byteLength"]]
    return Image.open(io.BytesIO(blob)).convert("RGBA")


def view_transform(point):
    yaw = math.radians(VIEW_YAW_DEG)
    pitch = math.radians(VIEW_PITCH_DEG)
    x, y, z = point
    vx = x * math.cos(yaw) - z * math.sin(yaw)
    vz = x * math.sin(yaw) + z * math.cos(yaw)
    vy = y * math.cos(pitch) - vz * math.sin(pitch)
    vd = y * math.sin(pitch) + vz * math.cos(pitch)
    return (vx, vy, vd)


def edge(a, b, x, y):
    return (x - a[0]) * (b[1] - a[1]) - (y - a[1]) * (b[0] - a[0])


def sample_texture(texture, u, v):
    if texture is None:
        shade = int(max(0, min(255, 96 + v * 120)))
        return (shade, shade, shade, 255)
    tw, th = texture.size
    u = u % 1.0
    v = v % 1.0
    tx = max(0, min(tw - 1, int(u * (tw - 1))))
    ty = max(0, min(th - 1, int((1.0 - v) * (th - 1))))
    return texture.getpixel((tx, ty))


def draw_triangle(img, zbuf, texture, verts, uvs):
    x0, y0, z0 = verts[0]
    x1, y1, z1 = verts[1]
    x2, y2, z2 = verts[2]
    area = edge((x0, y0), (x1, y1), x2, y2)
    if abs(area) < 0.01:
        return

    min_x = max(0, int(math.floor(min(x0, x1, x2))))
    max_x = min(SPRITE_W - 1, int(math.ceil(max(x0, x1, x2))))
    min_y = max(0, int(math.floor(min(y0, y1, y2))))
    max_y = min(SPRITE_H - 1, int(math.ceil(max(y0, y1, y2))))

    if max_x < min_x or max_y < min_y:
        return

    if max_x == min_x and max_y == min_y:
        px = min_x
        py = min_y
        u = (uvs[0][0] + uvs[1][0] + uvs[2][0]) / 3.0
        v = (uvs[0][1] + uvs[1][1] + uvs[2][1]) / 3.0
        z = (z0 + z1 + z2) / 3.0
        idx = py * SPRITE_W + px
        if z >= zbuf[idx]:
            zbuf[idx] = z
            img.putpixel((px, py), sample_texture(texture, u, v))
        return

    inv_area = 1.0 / area
    for py in range(min_y, max_y + 1):
        sy = py + 0.5
        for px in range(min_x, max_x + 1):
            sx = px + 0.5
            w0 = edge((x1, y1), (x2, y2), sx, sy) * inv_area
            w1 = edge((x2, y2), (x0, y0), sx, sy) * inv_area
            w2 = edge((x0, y0), (x1, y1), sx, sy) * inv_area
            if w0 < -0.001 or w1 < -0.001 or w2 < -0.001:
                continue

            z = z0 * w0 + z1 * w1 + z2 * w2
            idx = py * SPRITE_W + px
            if z < zbuf[idx]:
                continue
            zbuf[idx] = z

            u = uvs[0][0] * w0 + uvs[1][0] * w1 + uvs[2][0] * w2
            v = uvs[0][1] * w0 + uvs[1][1] * w1 + uvs[2][1] * w2
            img.putpixel((px, py), sample_texture(texture, u, v))


def fill_pinholes(image):
    filled = image
    for _ in range(2):
        alpha = filled.getchannel("A")
        grown = filled.filter(ImageFilter.MaxFilter(3))
        mask = alpha.point(lambda value: 255 if value == 0 else 0)
        expanded_alpha = alpha.filter(ImageFilter.MaxFilter(3))
        fill_mask = Image.composite(expanded_alpha, Image.new("L", image.size, 0), mask)
        next_img = filled.copy()
        next_img.paste(grown, mask=fill_mask)
        filled = next_img
    return filled


def add_outline(image):
    image = fill_pinholes(image)
    alpha = image.getchannel("A")
    outline = alpha.filter(ImageFilter.MaxFilter(3))
    shell = Image.new("RGBA", image.size, (7, 11, 18, 185))
    outlined = Image.new("RGBA", image.size, (0, 0, 0, 0))
    outlined.paste(shell, mask=outline)
    outlined.alpha_composite(image)
    return outlined


def render_billboard(path, width, height, yaw, pitch):
    global SPRITE_W, SPRITE_H, VIEW_YAW_DEG, VIEW_PITCH_DEG

    SPRITE_W = width
    SPRITE_H = height
    VIEW_YAW_DEG = yaw
    VIEW_PITCH_DEG = pitch

    gltf, bin_chunk = read_glb(path)
    texture = load_texture(gltf, bin_chunk)
    if texture is not None:
        texture = ImageEnhance.Contrast(texture).enhance(1.12)
        texture = ImageEnhance.Brightness(texture).enhance(1.08)

    meshes = gltf.get("meshes", [])
    all_positions = []
    primitives = []

    for mesh_index, matrix in collect_mesh_instances(gltf):
        mesh = meshes[mesh_index]
        for primitive in mesh.get("primitives", []):
            if primitive.get("mode", 4) != 4:
                continue
            attrs = primitive.get("attributes", {})
            pos_index = attrs.get("POSITION")
            uv_index = attrs.get("TEXCOORD_0")
            if pos_index is None:
                continue

            base = len(all_positions)
            positions = [view_transform(transform_point(matrix, p)) for p in read_accessor(gltf, bin_chunk, pos_index)]
            if uv_index is not None:
                texcoords = read_accessor(gltf, bin_chunk, uv_index)
            else:
                texcoords = [(0.5, 0.5)] * len(positions)
            all_positions.extend(positions)

            if "indices" in primitive:
                indices = [base + int(i) for i in read_accessor(gltf, bin_chunk, primitive["indices"])]
            else:
                indices = list(range(base, base + len(positions)))

            primitives.append({
                "base": base,
                "positions": positions,
                "texcoords": texcoords,
                "indices": indices,
            })

    if not all_positions:
        raise ValueError(f"{path} has no mesh data")

    min_x = min(p[0] for p in all_positions)
    max_x = max(p[0] for p in all_positions)
    min_y = min(p[1] for p in all_positions)
    max_y = max(p[1] for p in all_positions)
    scale = min((SPRITE_W - MARGIN * 2) / max(max_x - min_x, 1e-6),
                (SPRITE_H - MARGIN * 2) / max(max_y - min_y, 1e-6))
    center_x = (min_x + max_x) * 0.5

    def to_screen(view_point):
        vx, vy, vd = view_point
        return (
            SPRITE_W * 0.5 + (vx - center_x) * scale,
            SPRITE_H - MARGIN - (vy - min_y) * scale,
            vd,
        )

    img = Image.new("RGBA", (SPRITE_W, SPRITE_H), (0, 0, 0, 0))
    zbuf = [-1e30] * (SPRITE_W * SPRITE_H)

    for primitive in primitives:
        positions = primitive["positions"]
        texcoords = primitive["texcoords"]
        indices = primitive["indices"]
        triangle_count = len(indices) // 3
        step = max(1, math.ceil(triangle_count / MAX_RENDER_TRIANGLES))

        for tri in range(0, triangle_count, step):
            i = tri * 3
            ia = indices[i] - primitive["base"]
            ib = indices[i + 1] - primitive["base"]
            ic = indices[i + 2] - primitive["base"]
            draw_triangle(img, zbuf, texture,
                (to_screen(positions[ia]), to_screen(positions[ib]), to_screen(positions[ic])),
                (texcoords[ia], texcoords[ib], texcoords[ic]))

    return add_outline(img)


def main():
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    wrote_any = False

    for job in JOBS:
        paths = sorted(MODEL_DIR.glob(job["glob"]))
        if not paths:
            continue

        for path in paths:
            image = render_billboard(path, job["width"], job["height"], job["yaw"], job["pitch"])
            if "output" in job:
                out_name = job["output"]
            else:
                out_name = f"{path.stem}{job['suffix']}.png"
            out_path = OUT_DIR / out_name
            image.save(out_path)
            wrote_any = True
            print(f"{path.name}: wrote {out_path.relative_to(ROOT)}")

    if not wrote_any:
        raise SystemExit(f"No configured GLB files found in {MODEL_DIR}")


if __name__ == "__main__":
    main()
