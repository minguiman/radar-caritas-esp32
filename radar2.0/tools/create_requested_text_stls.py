import math
from pathlib import Path

import bpy
from mathutils import Vector


ROOT = Path(__file__).resolve().parents[1]

TEXTS = [
    {
        "name": "esp32_s3_lcd_qmi8658c_text",
        "body": "ESP32 S3 2.1 inch\nLCD 480x480 LVGL HMI\nQMI8658C 6-Axis Sensor",
        "size": 0.22,
        "extrude": 0.030,
        "bevel": 0.003,
        "line_spacing": 0.78,
    },
    {
        "name": "adsb_exchange_track_aircraft_text",
        "body": "ADS-B Exchange\ntrack aircraft live",
        "size": 0.28,
        "extrude": 0.035,
        "bevel": 0.004,
        "line_spacing": 0.82,
    },
]


def clear_scene():
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete()


def create_material():
    mat = bpy.data.materials.new("raised_text_preview")
    mat.diffuse_color = (0.86, 0.80, 0.62, 1.0)
    return mat


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


def add_text_mesh(spec, material):
    bpy.ops.object.text_add(location=(0.0, 0.0, 0.0), rotation=(math.radians(90.0), 0.0, 0.0))
    obj = bpy.context.object
    obj.name = spec["name"]
    obj.data.name = f"{spec['name']}_Curve"
    obj.data.body = spec["body"]
    obj.data.align_x = "CENTER"
    obj.data.align_y = "CENTER"
    obj.data.size = spec["size"]
    obj.data.space_line = spec["line_spacing"]
    obj.data.extrude = spec["extrude"]
    obj.data.bevel_depth = spec["bevel"]
    obj.data.bevel_resolution = 2
    obj.data.resolution_u = 24
    obj.data.materials.append(material)
    bpy.ops.object.convert(target="MESH")
    obj = bpy.context.object
    obj.name = spec["name"]
    obj.data.name = f"{spec['name']}_Mesh"
    center_mesh_on_origin(obj)
    return obj


def export_one(spec):
    clear_scene()
    material = create_material()
    obj = add_text_mesh(spec, material)
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj

    stl_path = ROOT / f"{spec['name']}.stl"
    blend_path = ROOT / f"{spec['name']}.blend"
    bpy.ops.wm.stl_export(filepath=str(stl_path), export_selected_objects=True)
    bpy.ops.wm.save_as_mainfile(filepath=str(blend_path))
    return stl_path, blend_path


for text_spec in TEXTS:
    export_one(text_spec)
