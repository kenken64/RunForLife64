import os
import sys

import bpy

SCRAPPER_OBJECTS = {"player", "backpack", "hair", "visor", "tools"}
DEFAULT_COLORS = [
    (0.74, 0.34, 0.22, 1.0),
    (0.18, 0.18, 0.20, 1.0),
    (0.07, 0.06, 0.05, 1.0),
    (0.12, 0.50, 0.65, 1.0),
    (0.52, 0.54, 0.56, 1.0),
]


def simple_material(original, fallback_index):
    color = DEFAULT_COLORS[fallback_index % len(DEFAULT_COLORS)]
    if original and hasattr(original, "diffuse_color"):
        color = tuple(original.diffuse_color)

    material = bpy.data.materials.new(f"rfl_flat_{fallback_index}")
    material.diffuse_color = color
    material.use_nodes = True
    bsdf = material.node_tree.nodes.get("Principled BSDF")
    if bsdf:
        bsdf.inputs["Base Color"].default_value = color
        bsdf.inputs["Alpha"].default_value = color[3]
        bsdf.inputs["Roughness"].default_value = 0.75
    return material


def mesh_triangle_count(obj):
    obj.data.calc_loop_triangles()
    return len(obj.data.loop_triangles)


def decimate_mesh(obj, ratio):
    bpy.context.view_layer.objects.active = obj
    obj.select_set(True)
    modifier = obj.modifiers.new("rfl_lod", "DECIMATE")
    modifier.ratio = max(0.0001, min(1.0, ratio))
    bpy.ops.object.modifier_apply(modifier=modifier.name)


def main() -> int:
    try:
        sep = sys.argv.index("--")
        output_path = sys.argv[sep + 1]
        max_triangles = int(sys.argv[sep + 2]) if len(sys.argv) > sep + 2 else 0
    except (ValueError, IndexError):
        print("usage: blender --background input.blend --python tools/blend_export_glb.py -- output.glb [max_triangles]")
        return 2

    os.makedirs(os.path.dirname(output_path), exist_ok=True)

    input_path = bpy.data.filepath.lower().replace("\\", "/")
    is_scrapper = input_path.endswith("/scrapper_kid.blend")

    export_objects = []
    for obj in list(bpy.context.scene.objects):
        should_export = obj.type == "MESH"
        if is_scrapper:
            should_export = obj.name in SCRAPPER_OBJECTS

        if not should_export:
            bpy.data.objects.remove(obj, do_unlink=True)
            continue

        export_objects.append(obj)

    if max_triangles > 0:
        total_triangles = sum(mesh_triangle_count(obj) for obj in export_objects)
        if total_triangles > max_triangles:
            ratio = max_triangles / total_triangles
            for obj in export_objects:
                decimate_mesh(obj, ratio)
            post_triangles = sum(mesh_triangle_count(obj) for obj in export_objects)
            print(f"decimated {total_triangles} triangles to {post_triangles} (target {max_triangles})")

    for obj in export_objects:
        obj.select_set(True)
        if obj.type == "MESH":
            obj.data = obj.data.copy()
            if len(obj.material_slots) == 0:
                obj.data.materials.append(simple_material(None, 0))
            for idx, slot in enumerate(obj.material_slots):
                slot.material = simple_material(slot.material, idx)

    bpy.ops.export_scene.gltf(
        filepath=output_path,
        export_format="GLB",
        use_selection=False,
        export_apply=True,
        export_yup=True,
        export_animations=False,
        export_skins=False,
        export_materials="EXPORT",
    )

    print(f"exported {output_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
