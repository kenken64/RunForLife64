#!/usr/bin/env python3
import io
import json
import math
import re
import struct
from pathlib import Path

from PIL import Image


ROOT = Path(__file__).resolve().parents[1]
MODEL_DIR = ROOT / "assets" / "models"
OUT_C = ROOT / "src" / "building_models.c"
OUT_H = ROOT / "src" / "building_models.h"

MODEL_GLOB = "building*.glb"
PLAYER_MODEL = "backtofuture.glb"
BUILDING_TRIANGLE_LIMIT = 32
PLAYER_TRIANGLE_LIMIT = 96
MODEL_SCALE = 1024.0

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


def load_images(gltf, bin_chunk):
    images = []
    for image in gltf.get("images", []):
        if "bufferView" not in image:
            images.append(None)
            continue

        view = gltf["bufferViews"][image["bufferView"]]
        start = view.get("byteOffset", 0)
        blob = bin_chunk[start:start + view["byteLength"]]
        try:
            images.append(Image.open(io.BytesIO(blob)).convert("RGBA"))
        except Exception:
            images.append(None)
    return images


def sample_image(image, u, v):
    if image is None:
        return (255, 255, 255)

    tw, th = image.size
    u = u % 1.0
    v = v % 1.0
    tx = max(0, min(tw - 1, int(u * (tw - 1))))
    ty = max(0, min(th - 1, int((1.0 - v) * (th - 1))))
    r, g, b, _ = image.getpixel((tx, ty))
    return (r, g, b)


def material_sampler(gltf, images, material_index):
    if material_index is None:
        return ((0.70, 0.78, 0.90), None)

    material = gltf.get("materials", [])[material_index]
    pbr = material.get("pbrMetallicRoughness", {})
    factor = pbr.get("baseColorFactor", [1.0, 1.0, 1.0, 1.0])
    texture = None

    texture_info = pbr.get("baseColorTexture")
    if texture_info is not None:
        texture_index = texture_info.get("index")
        textures = gltf.get("textures", [])
        if texture_index is not None and texture_index < len(textures):
            source = textures[texture_index].get("source")
            if source is not None and source < len(images):
                texture = images[source]

    return ((factor[0], factor[1], factor[2]), texture)


def triangle_color(base_color, texture, texcoords):
    if texcoords is None:
        sample = (255, 255, 255)
    else:
        u = (texcoords[0][0] + texcoords[1][0] + texcoords[2][0]) / 3.0
        v = (texcoords[0][1] + texcoords[1][1] + texcoords[2][1]) / 3.0
        sample = sample_image(texture, u, v)

    return (
        int(sample[0] * base_color[0]),
        int(sample[1] * base_color[1]),
        int(sample[2] * base_color[2]),
    )


def identity_matrix():
    return [
        1.0, 0.0, 0.0, 0.0,
        0.0, 1.0, 0.0, 0.0,
        0.0, 0.0, 1.0, 0.0,
        0.0, 0.0, 0.0, 1.0,
    ]


def matrix_from_gltf(values):
    # glTF stores matrices column-major; use row-major internally.
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


def transform_point(matrix, p):
    x, y, z = p
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

    if scenes:
        roots = scenes[gltf.get("scene", 0)].get("nodes", [])
    else:
        roots = list(range(len(nodes)))

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


def sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def cross(a, b):
    return (
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0],
    )


def length(v):
    return math.sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2])


def normal_and_area(p0, p1, p2):
    n = cross(sub(p1, p0), sub(p2, p0))
    nlen = length(n)
    if nlen < 1e-8:
        return None, 0.0
    return (n[0] / nlen, n[1] / nlen, n[2] / nlen), nlen * 0.5


def clamp(value, low, high):
    return max(low, min(high, value))


def triangle_bucket(center, normal, bounds):
    min_x, min_y, min_z, max_x, max_y, max_z = bounds
    ex = max(max_x - min_x, 1e-6)
    ey = max(max_y - min_y, 1e-6)
    ez = max(max_z - min_z, 1e-6)
    cx = clamp((center[0] - min_x) / ex, 0.0, 0.999)
    cy = clamp((center[1] - min_y) / ey, 0.0, 0.999)
    cz = clamp((center[2] - min_z) / ez, 0.0, 0.999)

    ax, ay, az = abs(normal[0]), abs(normal[1]), abs(normal[2])
    if ay > ax and ay > az:
        return ("y", 1 if normal[1] >= 0.0 else -1, int(cx * 6), int(cz * 6))
    if ax > az:
        return ("x", 1 if normal[0] >= 0.0 else -1, int(cy * 12), int(cz * 6))
    return ("z", 1 if normal[2] >= 0.0 else -1, int(cy * 12), int(cx * 6))


def triangle_light(normal):
    light = (-0.35, 0.78, -0.52)
    dot = normal[0] * light[0] + normal[1] * light[1] + normal[2] * light[2]
    return 0.42 + clamp(dot, -0.35, 1.0) * 0.58


def lit_color(color, normal):
    light = triangle_light(normal)
    ambient = 28
    return (
        clamp(int(color[0] * light) + ambient, 18, 255),
        clamp(int(color[1] * light) + ambient, 18, 255),
        clamp(int(color[2] * light) + ambient, 18, 255),
    )


def create_patch_accumulator(bucket):
    return {
        "bucket": bucket,
        "area": 0.0,
        "min_x": math.inf,
        "min_y": math.inf,
        "min_z": math.inf,
        "max_x": -math.inf,
        "max_y": -math.inf,
        "max_z": -math.inf,
        "center_x": 0.0,
        "center_y": 0.0,
        "center_z": 0.0,
        "r": 0.0,
        "g": 0.0,
        "b": 0.0,
    }


def add_triangle_to_patch(accumulator, points, center, color, area):
    accumulator["area"] += area
    accumulator["center_x"] += center[0] * area
    accumulator["center_y"] += center[1] * area
    accumulator["center_z"] += center[2] * area
    accumulator["r"] += color[0] * area
    accumulator["g"] += color[1] * area
    accumulator["b"] += color[2] * area

    for point in points:
        accumulator["min_x"] = min(accumulator["min_x"], point[0])
        accumulator["min_y"] = min(accumulator["min_y"], point[1])
        accumulator["min_z"] = min(accumulator["min_z"], point[2])
        accumulator["max_x"] = max(accumulator["max_x"], point[0])
        accumulator["max_y"] = max(accumulator["max_y"], point[1])
        accumulator["max_z"] = max(accumulator["max_z"], point[2])


def inflate_range(low, high, minimum_span, model_low, model_high):
    span = high - low
    if span >= minimum_span:
        return low, high

    center = (low + high) * 0.5
    low = center - minimum_span * 0.5
    high = center + minimum_span * 0.5

    if low < model_low:
        high += model_low - low
        low = model_low
    if high > model_high:
        low -= high - model_high
        high = model_high

    return low, high


def patch_to_triangles(accumulator, bounds):
    area = max(accumulator["area"], 1e-6)
    axis, _sign, _u, _v = accumulator["bucket"]
    min_x, min_y, min_z, max_x, max_y, max_z = bounds
    min_span_x = max((max_x - min_x) * 0.08, 1e-5)
    min_span_y = max((max_y - min_y) * 0.08, 1e-5)
    min_span_z = max((max_z - min_z) * 0.08, 1e-5)
    color = (
        int(accumulator["r"] / area),
        int(accumulator["g"] / area),
        int(accumulator["b"] / area),
    )

    cx = accumulator["center_x"] / area
    cy = accumulator["center_y"] / area
    cz = accumulator["center_z"] / area

    if axis == "x":
        y0, y1 = inflate_range(accumulator["min_y"], accumulator["max_y"], min_span_y, min_y, max_y)
        z0, z1 = inflate_range(accumulator["min_z"], accumulator["max_z"], min_span_z, min_z, max_z)
        points = [
            ((cx, y0, z0), (cx, y1, z0), (cx, y1, z1)),
            ((cx, y0, z0), (cx, y1, z1), (cx, y0, z1)),
        ]
    elif axis == "y":
        x0, x1 = inflate_range(accumulator["min_x"], accumulator["max_x"], min_span_x, min_x, max_x)
        z0, z1 = inflate_range(accumulator["min_z"], accumulator["max_z"], min_span_z, min_z, max_z)
        points = [
            ((x0, cy, z0), (x1, cy, z0), (x1, cy, z1)),
            ((x0, cy, z0), (x1, cy, z1), (x0, cy, z1)),
        ]
    else:
        x0, x1 = inflate_range(accumulator["min_x"], accumulator["max_x"], min_span_x, min_x, max_x)
        y0, y1 = inflate_range(accumulator["min_y"], accumulator["max_y"], min_span_y, min_y, max_y)
        points = [
            ((x0, y0, cz), (x1, y0, cz), (x1, y1, cz)),
            ((x0, y0, cz), (x1, y1, cz), (x0, y1, cz)),
        ]

    return [
        {
            "area": accumulator["area"],
            "center": (
                sum(point[0] for point in tri) / 3.0,
                sum(point[1] for point in tri) / 3.0,
                sum(point[2] for point in tri) / 3.0,
            ),
            "points": tri,
            "color": color,
        }
        for tri in points
    ]


def sanitize_name(path):
    return re.sub(r"[^a-zA-Z0-9_]", "_", path.stem).lower()


def load_triangles(path, triangle_limit):
    gltf, bin_chunk = read_glb(path)
    images = load_images(gltf, bin_chunk)
    meshes = gltf.get("meshes", [])
    points = []
    primitives = []

    for mesh_index, matrix in collect_mesh_instances(gltf):
        mesh = meshes[mesh_index]
        for primitive in mesh.get("primitives", []):
            if primitive.get("mode", 4) != 4:
                continue
            pos_index = primitive.get("attributes", {}).get("POSITION")
            if pos_index is None:
                continue

            uv_index = primitive.get("attributes", {}).get("TEXCOORD_0")
            base_color, texture = material_sampler(gltf, images, primitive.get("material"))
            base = len(points)
            positions = read_accessor(gltf, bin_chunk, pos_index)
            points.extend(transform_point(matrix, p) for p in positions)
            texcoords = read_accessor(gltf, bin_chunk, uv_index) if uv_index is not None else None
            if "indices" in primitive:
                indices = [base + int(i) for i in read_accessor(gltf, bin_chunk, primitive["indices"])]
            else:
                indices = list(range(base, base + len(positions)))
            primitives.append({
                "base": base,
                "indices": indices,
                "texcoords": texcoords,
                "base_color": base_color,
                "texture": texture,
            })

    if not points or not primitives:
        raise ValueError(f"{path} has no triangle mesh data")

    xs = [p[0] for p in points]
    ys = [p[1] for p in points]
    zs = [p[2] for p in points]
    bounds = (min(xs), min(ys), min(zs), max(xs), max(ys), max(zs))

    patches = {}
    for primitive in primitives:
        indices = primitive["indices"]
        for i in range(0, len(indices) - 2, 3):
            p0 = points[indices[i]]
            p1 = points[indices[i + 1]]
            p2 = points[indices[i + 2]]
            normal, area = normal_and_area(p0, p1, p2)
            if normal is None:
                continue
            center = (
                (p0[0] + p1[0] + p2[0]) / 3.0,
                (p0[1] + p1[1] + p2[1]) / 3.0,
                (p0[2] + p1[2] + p2[2]) / 3.0,
            )

            texcoords = None
            if primitive["texcoords"] is not None:
                base = primitive["base"]
                texcoords = (
                    primitive["texcoords"][indices[i] - base],
                    primitive["texcoords"][indices[i + 1] - base],
                    primitive["texcoords"][indices[i + 2] - base],
                )
            color = triangle_color(primitive["base_color"], primitive["texture"], texcoords)
            bucket = triangle_bucket(center, normal, bounds)
            accumulator = patches.get(bucket)
            if accumulator is None:
                accumulator = create_patch_accumulator(bucket)
                patches[bucket] = accumulator

            add_triangle_to_patch(accumulator, (p0, p1, p2), center, lit_color(color, normal), area)

    selected = []
    patch_limit = max(1, triangle_limit // 2)
    for accumulator in sorted(patches.values(), key=lambda item: item["area"], reverse=True)[:patch_limit]:
        selected.extend(patch_to_triangles(accumulator, bounds))

    selected = selected[:triangle_limit]
    selected.sort(key=lambda item: item["center"][2], reverse=True)
    return bounds, selected


def normalize_model(bounds, triangles):
    min_x, min_y, min_z, max_x, max_y, max_z = bounds
    center_x = (min_x + max_x) * 0.5
    center_z = (min_z + max_z) * 0.5
    height = max(max_y - min_y, 1e-6)
    footprint = max(max_x - min_x, max_z - min_z, 1e-6)

    vertices = []
    vertex_map = {}
    out_tris = []

    def add_vertex(point):
        x = int(round(((point[0] - center_x) / footprint) * MODEL_SCALE))
        y = int(round(((point[1] - min_y) / height) * MODEL_SCALE))
        z = int(round(((point[2] - center_z) / footprint) * MODEL_SCALE))
        key = (clamp(x, -32768, 32767), clamp(y, -32768, 32767), clamp(z, -32768, 32767))
        if key not in vertex_map:
            vertex_map[key] = len(vertices)
            vertices.append(key)
        return vertex_map[key]

    for tri in triangles:
        a = add_vertex(tri["points"][0])
        b = add_vertex(tri["points"][1])
        c = add_vertex(tri["points"][2])
        if a != b and b != c and a != c:
            r, g, b_color = tri["color"]
            out_tris.append((a, b, c, r, g, b_color))

    return vertices, out_tris


def write_outputs(building_models, player_model):
    OUT_H.write_text("""#ifndef RUNFORLIFE64_BUILDING_MODELS_H
#define RUNFORLIFE64_BUILDING_MODELS_H

#include <stdint.h>

typedef struct {
    int16_t x;
    int16_t y;
    int16_t z;
} RflModelVertex;

typedef struct {
    uint16_t a;
    uint16_t b;
    uint16_t c;
    uint8_t r;
    uint8_t g;
    uint8_t blue;
} RflModelTriangle;

typedef struct {
    const RflModelVertex *vertices;
    const RflModelTriangle *triangles;
    uint16_t vertex_count;
    uint16_t triangle_count;
} RflStaticModel;

extern const RflStaticModel rfl_building_models[];
extern const uint16_t rfl_building_model_count;
extern const RflStaticModel rfl_player_model;

#endif
""")

    lines = [
        '#include "building_models.h"',
        "",
        "/* Generated by tools/glb_to_rfl_mesh.py from the GLB model assets. */",
        "",
    ]

    for model in [*building_models, player_model]:
        name = model["name"]
        lines.append(f"static const RflModelVertex {name}_vertices[] = {{")
        for x, y, z in model["vertices"]:
            lines.append(f"    {{ {x}, {y}, {z} }},")
        lines.append("};")
        lines.append("")
        lines.append(f"static const RflModelTriangle {name}_triangles[] = {{")
        for a, b, c, r, g, blue in model["triangles"]:
            lines.append(f"    {{ {a}, {b}, {c}, {r}, {g}, {blue} }},")
        lines.append("};")
        lines.append("")

    lines.append("const RflStaticModel rfl_building_models[] = {")
    for model in building_models:
        name = model["name"]
        lines.append(
            f"    {{ {name}_vertices, {name}_triangles, "
            f"(uint16_t)(sizeof({name}_vertices) / sizeof({name}_vertices[0])), "
            f"(uint16_t)(sizeof({name}_triangles) / sizeof({name}_triangles[0])) }},"
        )
    lines.append("};")
    lines.append("")
    lines.append("const uint16_t rfl_building_model_count =")
    lines.append("    (uint16_t)(sizeof(rfl_building_models) / sizeof(rfl_building_models[0]));")
    lines.append("")
    name = player_model["name"]
    lines.append("const RflStaticModel rfl_player_model =")
    lines.append(
        f"    {{ {name}_vertices, {name}_triangles, "
        f"(uint16_t)(sizeof({name}_vertices) / sizeof({name}_vertices[0])), "
        f"(uint16_t)(sizeof({name}_triangles) / sizeof({name}_triangles[0])) }};"
    )
    lines.append("")

    OUT_C.write_text("\n".join(lines))


def main():
    paths = sorted(MODEL_DIR.glob(MODEL_GLOB))
    if not paths:
        raise SystemExit(f"No {MODEL_GLOB} files found in assets/models")

    building_models = []
    for path in paths:
        bounds, selected = load_triangles(path, BUILDING_TRIANGLE_LIMIT)
        vertices, triangles = normalize_model(bounds, selected)
        building_models.append({
            "name": sanitize_name(path),
            "source": path.name,
            "vertices": vertices,
            "triangles": triangles,
        })
        print(f"{path.name}: {len(vertices)} vertices, {len(triangles)} triangles")

    player_path = MODEL_DIR / PLAYER_MODEL
    if not player_path.exists():
        raise SystemExit(f"{player_path.relative_to(ROOT)} not found")

    bounds, selected = load_triangles(player_path, PLAYER_TRIANGLE_LIMIT)
    vertices, triangles = normalize_model(bounds, selected)
    player_model = {
        "name": sanitize_name(player_path),
        "source": player_path.name,
        "vertices": vertices,
        "triangles": triangles,
    }
    print(f"{player_path.name}: {len(vertices)} vertices, {len(triangles)} triangles")

    write_outputs(building_models, player_model)
    print(f"wrote {OUT_H.relative_to(ROOT)} and {OUT_C.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
