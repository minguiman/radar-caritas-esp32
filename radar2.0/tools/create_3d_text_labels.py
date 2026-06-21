import math
from pathlib import Path

import bpy


ROOT = Path(__file__).resolve().parents[1]
BLEND_PATH = ROOT / "mpds_oceans_sky_text.blend"
STL_PATH = ROOT / "mpds_oceans_sky_text.stl"
OBJ_PATH = ROOT / "mpds_oceans_sky_text.obj"


def clear_scene():
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete()


def make_material(name, color):
    mat = bpy.data.materials.new(name)
    mat.diffuse_color = color
    return mat


def add_text(name, body, location, size, extrude, bevel, material):
    bpy.ops.object.text_add(location=location, rotation=(math.radians(90.0), 0.0, 0.0))
    obj = bpy.context.object
    obj.name = name
    obj.data.name = f"{name}_Curve"
    obj.data.body = body
    obj.data.align_x = "CENTER"
    obj.data.align_y = "CENTER"
    obj.data.size = size
    obj.data.extrude = extrude
    obj.data.bevel_depth = bevel
    obj.data.bevel_resolution = 2
    obj.data.resolution_u = 24
    obj.data.materials.append(material)

    bpy.ops.object.convert(target="MESH")
    obj = bpy.context.object
    obj.name = name
    obj.data.name = f"{name}_Mesh"
    return obj


def set_origin_geometry(obj):
    bpy.ops.object.select_all(action="DESELECT")
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.origin_set(type="ORIGIN_GEOMETRY", center="BOUNDS")


clear_scene()

cream = make_material("raised_cream_text", (0.86, 0.80, 0.62, 1.0))

mpds = add_text(
    "MPDS_3D_Text",
    "MPDS",
    (0.0, 0.0, 0.38),
    size=0.55,
    extrude=0.055,
    bevel=0.006,
    material=cream,
)

oceans = add_text(
    "OCEANS_SKY_3D_Text",
    "OCEANS SKY",
    (0.0, 0.0, -0.30),
    size=0.30,
    extrude=0.040,
    bevel=0.005,
    material=cream,
)

for obj in (mpds, oceans):
    set_origin_geometry(obj)

bpy.ops.object.select_all(action="DESELECT")
mpds.select_set(True)
oceans.select_set(True)
bpy.context.view_layer.objects.active = mpds

bpy.ops.wm.save_as_mainfile(filepath=str(BLEND_PATH))
bpy.ops.wm.stl_export(filepath=str(STL_PATH), export_selected_objects=True)
bpy.ops.wm.obj_export(filepath=str(OBJ_PATH), export_selected_objects=True)
