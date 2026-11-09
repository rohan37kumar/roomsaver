"""
Generates a low-poly PLACEHOLDER room and exports assets/room_placeholder.glb.

Run headless:
    blender -b -P tools/make_placeholder_room.py

Purpose:
  * Exercise the real Blender -> glTF -> engine pipeline used for the real room.
  * Establish the node-naming CONTRACT the engine binds dynamic content to:
      TV_Screen      - album art surface
      Picture_Image  - local wall picture
      Clock_Display  - live clock
      TV_BiasLight   - emissive bias light (album color + beat flicker)
      Lamp_Wall      - wall lamp (drives a warm point light)
      Soundbar / PSIcons - static emissive accents
  See docs/ASSET_CONTRACT.md.
  This geometry is intentionally crude; the real assets/room.glb replaces it.

Coordinate system: Blender Z-up, meters. Room occupies x:[0,4] y:[0,4] z:[0,2.6].
Two walls meet at the (x=0, y=4) corner; the open side faces (+x, -y).
Export uses +Y up (glTF convention), so the engine receives a Y-up scene.
"""

import bpy
import os
from math import radians

OUT = os.path.normpath(os.path.join(os.path.dirname(__file__), "..", "assets", "room_placeholder.glb"))


def clear_scene():
    for obj in list(bpy.data.objects):
        bpy.data.objects.remove(obj, do_unlink=True)
    for m in list(bpy.data.materials):
        bpy.data.materials.remove(m, do_unlink=True)


def material(name, color, emissive=None, emissive_strength=0.0):
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    bsdf = mat.node_tree.nodes.get("Principled BSDF")
    if bsdf:
        bsdf.inputs["Base Color"].default_value = (*color, 1.0)
        try:
            bsdf.inputs["Roughness"].default_value = 0.7
        except Exception:
            pass
        if emissive is not None:
            for key in ("Emission Color", "Emission"):
                if key in bsdf.inputs:
                    bsdf.inputs[key].default_value = (*emissive, 1.0)
                    break
            if "Emission Strength" in bsdf.inputs:
                bsdf.inputs["Emission Strength"].default_value = emissive_strength
    return mat


def assign(obj, mat):
    obj.data.materials.clear()
    obj.data.materials.append(mat)


def box(name, center, size, mat):
    bpy.ops.mesh.primitive_cube_add(size=1.0, location=center)
    o = bpy.context.active_object
    o.name = name
    o.scale = (size[0], size[1], size[2])
    assign(o, mat)
    return o


def quad(name, center, width, height, axis, mat):
    """Flat plane facing -Y (axis='y') or +X (axis='x')."""
    bpy.ops.mesh.primitive_plane_add(size=1.0, location=center)
    o = bpy.context.active_object
    o.name = name
    if axis == "y":
        o.rotation_euler = (radians(90), 0, 0)   # normal -> -Y
        o.scale = (width, height, 1.0)
    else:  # 'x'
        o.rotation_euler = (0, radians(90), 0)   # normal -> +X
        o.scale = (1.0, width, height)
    assign(o, mat)
    return o


def build():
    clear_scene()

    m_floor   = material("M_Floor",   (0.05, 0.045, 0.05))
    m_wall    = material("M_Wall",    (0.20, 0.19, 0.21))
    m_wood    = material("M_Wood",    (0.30, 0.19, 0.11))
    m_sofa    = material("M_Sofa",    (0.04, 0.04, 0.05))
    m_white   = material("M_White",   (0.85, 0.85, 0.85))
    m_tv      = material("M_TVBody",  (0.02, 0.02, 0.02))
    m_screen  = material("M_Screen",  (0.10, 0.11, 0.13))   # replaced by album art at runtime
    m_picture = material("M_Picture", (0.15, 0.18, 0.30))   # replaced by local image
    m_clock   = material("M_Clock",   (0.01, 0.01, 0.01))
    m_plant   = material("M_Plant",   (0.10, 0.30, 0.12))

    m_bias    = material("M_Bias",     (0.9, 0.7, 0.4), emissive=(0.9, 0.7, 0.4), emissive_strength=3.0)
    m_lamp    = material("M_Lamp",     (1.0, 0.9, 0.7), emissive=(1.0, 0.9, 0.7), emissive_strength=4.0)
    m_soundbar= material("M_Soundbar", (0.9, 0.95, 1.0), emissive=(0.9, 0.95, 1.0), emissive_strength=2.0)
    m_psicons = material("M_PSIcons",  (0.9, 0.2, 0.8),  emissive=(0.9, 0.2, 0.8),  emissive_strength=3.0)

    # shell
    box("Floor",     (2.0, 2.0, -0.05), (4.0, 4.0, 0.10), m_floor)
    box("Wall_Left", (0.05, 2.0, 1.30), (0.10, 4.0, 2.60), m_wall)   # picture wall (x=0)
    box("Wall_Back", (2.0, 3.95, 1.30), (4.0, 0.10, 2.60), m_wall)   # TV wall (y=4)

    # TV wall cluster
    box("TV_Body",     (2.6, 3.90, 1.65), (1.5, 0.10, 0.85), m_tv)
    quad("TV_Screen",  (2.6, 3.83, 1.65), 1.40, 0.78, "y", m_screen)
    box("TV_BiasLight",(2.6, 3.94, 1.65), (1.6, 0.03, 0.95), m_bias)

    box("Console",     (2.6, 3.72, 0.60), (2.0, 0.45, 0.10), m_wood)
    box("Clock_Body",  (1.85, 3.62, 0.74), (0.28, 0.12, 0.16), m_clock)
    quad("Clock_Display",(1.85, 3.55, 0.74), 0.24, 0.13, "y", m_clock)
    box("Soundbar",    (2.7, 3.60, 0.72), (0.9, 0.08, 0.08), m_soundbar)
    box("PSIcons",     (3.25, 3.60, 0.73), (0.35, 0.06, 0.10), m_psicons)

    # picture wall cluster
    box("Picture_Frame",(0.10, 1.20, 1.60), (0.06, 0.95, 1.15), m_white)
    quad("Picture_Image",(0.14, 1.20, 1.60), 0.85, 1.05, "x", m_picture)
    box("Lamp_Wall",   (0.14, 2.30, 1.70), (0.08, 0.16, 0.36), m_lamp)

    # furniture
    box("Sofa_Seat",   (1.4, 1.05, 0.35), (1.9, 0.85, 0.35), m_sofa)
    box("Sofa_Back",   (1.4, 0.72, 0.75), (1.9, 0.22, 0.55), m_sofa)
    box("CoffeeTable", (1.7, 1.95, 0.30), (1.05, 0.6, 0.08), m_white)
    box("Plant",       (3.6, 3.5, 0.35), (0.35, 0.35, 0.70), m_plant)

    print("Placeholder room built: %d objects" % len(bpy.data.objects))


def export():
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    bpy.ops.export_scene.gltf(
        filepath=OUT,
        export_format='GLB',
        use_selection=False,
        export_apply=True,
        export_yup=True,
    )
    print("Exported:", OUT)


if __name__ == "__main__":
    build()
    export()
