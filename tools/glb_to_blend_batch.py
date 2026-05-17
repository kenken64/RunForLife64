#!/usr/bin/env python3
import sys
from pathlib import Path

import bpy


def clear_scene():
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete()

    for collection in (
        bpy.data.meshes,
        bpy.data.materials,
        bpy.data.images,
        bpy.data.textures,
        bpy.data.armatures,
        bpy.data.actions,
    ):
        for item in list(collection):
            collection.remove(item)


def convert_glb(path: Path):
    clear_scene()
    out_path = path.with_suffix(".blend")

    bpy.ops.import_scene.gltf(filepath=str(path))
    bpy.ops.wm.save_as_mainfile(filepath=str(out_path), compress=True)
    print(f"converted {path} -> {out_path}")


def main():
    if "--" not in sys.argv:
        raise SystemExit("Usage: blender --background --python tools/glb_to_blend_batch.py -- <glb files>")

    paths = [Path(arg) for arg in sys.argv[sys.argv.index("--") + 1:]]
    if not paths:
        raise SystemExit("No GLB files provided")

    for path in paths:
        convert_glb(path)


if __name__ == "__main__":
    main()
