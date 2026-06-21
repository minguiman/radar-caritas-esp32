import math
from pathlib import Path

import bpy
from mathutils import Vector


ROOT = Path(__file__).resolve().parents[1]
NAME = "adsb_aircraft_track_text"
BODY = "ADS-B\nAircraft  Track"


def clear_scene():
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete()


def center_mesh_on_origin(obj):
    world_corners = [obj.matrix_world @ Vector(corner) for corner in obj.bound_box]
    center = sum(world_corners, Vector()) / len(world_corners)
    obj.location -= center
    bpy.context.view_layer.update()
    bpy.ops.object.select_all(action="DESELECT")
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.transform_apply(location=True, rotation=False, scale=False)
    bpy.ops.object.origin_set(type="ORIGIN_GEOMETRY", center="BOUNDS")


clear_scene()

mat = bpy.data.materials.new("raised_text_preview")
mat.diffuse_color = (0.86, 0.80, 0.62, 1.0)

bpy.ops.object.text_add(location=(0.0, 0.0, 0.0), rotation=(math.radians(90.0), 0.0, 0.0))
obj = bpy.context.object
obj.name = NAME
obj.data.name = f"{NAME}_Curve"
obj.data.body = BODY
obj.data.align_x = "CENTER"
obj.data.align_y = "CENTER"
obj.data.size = 0.32
obj.data.space_line = 0.82
obj.data.extrude = 0.035
obj.data.bevel_depth = 0.004
obj.data.bevel_resolution = 2
obj.data.resolution_u = 24
obj.data.materials.append(mat)

bpy.ops.object.convert(target="MESH")
obj = bpy.context.object
obj.name = NAME
obj.data.name = f"{NAME}_Mesh"
center_mesh_on_origin(obj)

stl_path = ROOT / f"{NAME}.stl"
blend_path = ROOT / f"{NAME}.blend"
bpy.ops.wm.stl_export(filepath=str(stl_path), export_selected_objects=True)
bpy.ops.wm.save_as_mainfile(filepath=str(blend_path))
