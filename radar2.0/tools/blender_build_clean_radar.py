import math

import bpy
from mathutils import Vector


SOURCE_NAME = args.get("source_name")
RESULT_NAME = args.get("result_name", "Radar_Clean_Print")
COLLECTION_NAME = args.get("collection_name", "RadarCleanRebuild")


def scene_bounds(obj):
    verts = [obj.matrix_world @ v.co for v in obj.data.vertices]
    min_v = Vector((min(v.x for v in verts), min(v.y for v in verts), min(v.z for v in verts)))
    max_v = Vector((max(v.x for v in verts), max(v.y for v in verts), max(v.z for v in verts)))
    return min_v, max_v


def ensure_collection(name):
    col = bpy.data.collections.get(name)
    if col is None:
        col = bpy.data.collections.new(name)
        bpy.context.scene.collection.children.link(col)
    return col


def activate(obj):
    bpy.ops.object.select_all(action="DESELECT")
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj


def link_only_to_collection(obj, collection):
    for col in list(obj.users_collection):
        col.objects.unlink(obj)
    collection.objects.link(obj)


def apply_scale(obj):
    activate(obj)
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)


def material(name, color):
    mat = bpy.data.materials.get(name)
    if mat is None:
        mat = bpy.data.materials.new(name)
        mat.diffuse_color = color
    return mat


MAT_BODY = material("radar_olive_print_preview", (0.33, 0.34, 0.24, 1.0))
MAT_DARK = material("radar_dark_insets_preview", (0.035, 0.035, 0.03, 1.0))
MAT_METAL = material("radar_worn_metal_preview", (0.56, 0.52, 0.43, 1.0))
MAT_RED = material("radar_red_lens_preview", (0.55, 0.05, 0.035, 1.0))
MAT_TEXT = material("radar_raised_text_preview", (0.86, 0.80, 0.62, 1.0))


def assign(obj, mat):
    obj.data.materials.append(mat)
    for poly in obj.data.polygons:
        poly.material_index = len(obj.data.materials) - 1


def create_box(name, location, size_xyz, collection, mat=MAT_BODY, bevel=0.0, segments=3):
    bpy.ops.mesh.primitive_cube_add(location=location)
    obj = bpy.context.active_object
    obj.name = name
    obj.scale = (size_xyz[0] / 2.0, size_xyz[1] / 2.0, size_xyz[2] / 2.0)
    apply_scale(obj)
    assign(obj, mat)
    link_only_to_collection(obj, collection)
    if bevel > 0:
        add_bevel(obj, bevel, segments)
    return obj


def create_cylinder(name, location, radius, depth, axis, collection, mat=MAT_BODY, vertices=64):
    rotation = (0.0, 0.0, 0.0)
    if axis == "Y":
        rotation = (math.radians(90.0), 0.0, 0.0)
    elif axis == "X":
        rotation = (0.0, math.radians(90.0), 0.0)
    bpy.ops.mesh.primitive_cylinder_add(
        vertices=vertices,
        radius=radius,
        depth=depth,
        location=location,
        rotation=rotation,
    )
    obj = bpy.context.active_object
    obj.name = name
    apply_scale(obj)
    assign(obj, mat)
    link_only_to_collection(obj, collection)
    return obj


def create_uv_sphere(name, location, radius, collection, mat=MAT_BODY):
    bpy.ops.mesh.primitive_uv_sphere_add(segments=40, ring_count=16, radius=radius, location=location)
    obj = bpy.context.active_object
    obj.name = name
    assign(obj, mat)
    link_only_to_collection(obj, collection)
    return obj


def create_ring(name, center, outer_radius, inner_radius, depth, collection, mat=MAT_BODY, segments=128):
    verts = []
    faces = []
    y_front = center[1] - depth / 2.0
    y_back = center[1] + depth / 2.0
    for i in range(segments):
        a = math.tau * i / segments
        ca = math.cos(a)
        sa = math.sin(a)
        verts.extend(
            [
                (center[0] + outer_radius * ca, y_front, center[2] + outer_radius * sa),
                (center[0] + outer_radius * ca, y_back, center[2] + outer_radius * sa),
                (center[0] + inner_radius * ca, y_front, center[2] + inner_radius * sa),
                (center[0] + inner_radius * ca, y_back, center[2] + inner_radius * sa),
            ]
        )
    for i in range(segments):
        n = (i + 1) % segments
        o0, o1, i0, i1 = i * 4, i * 4 + 1, i * 4 + 2, i * 4 + 3
        no0, no1, ni0, ni1 = n * 4, n * 4 + 1, n * 4 + 2, n * 4 + 3
        faces.append((o0, no0, no1, o1))
        faces.append((i0, i1, ni1, ni0))
        faces.append((o0, i0, ni0, no0))
        faces.append((o1, no1, ni1, i1))
    mesh = bpy.data.meshes.new(f"{name}Mesh")
    mesh.from_pydata(verts, [], faces)
    mesh.update()
    obj = bpy.data.objects.new(name, mesh)
    collection.objects.link(obj)
    assign(obj, mat)
    return obj


def create_text(name, text, location, size, collection, mat=MAT_TEXT, extrude=0.012, align="CENTER"):
    chars = list(text)
    char_w = size * 0.34
    char_h = size * 0.62
    spacing = char_w * 1.22
    total = max(1, len(chars) - 1) * spacing
    start_x = location[0] - total / 2.0 if align == "CENTER" else location[0]
    strokes = []

    for idx, ch in enumerate(chars):
        if ch == " ":
            continue
        cx = start_x + idx * spacing
        zc = location[2]
        code = ord(ch)
        bars = [
            (0.0, 0.28, 0.62, 0.10),
            (0.0, -0.28, 0.62, 0.10),
        ]
        if code % 2 == 0:
            bars.append((-0.23, 0.0, 0.10, 0.60))
        if code % 3 != 0:
            bars.append((0.23, 0.0, 0.10, 0.60))
        if code % 5 != 0:
            bars.append((0.0, 0.0, 0.52, 0.08))

        for bidx, (ox, oz, bw, bh) in enumerate(bars):
            strokes.append(
                create_box(
                    f"{name}_Stroke_{idx}_{bidx}",
                    (cx + ox * char_w, location[1], zc + oz * char_h),
                    (bw * char_w, extrude, bh * char_h),
                    collection,
                    mat,
                )
            )

    if not strokes:
        return create_box(name, location, (char_w, extrude, char_h), collection, mat)

    bpy.ops.object.select_all(action="DESELECT")
    strokes[0].select_set(True)
    bpy.context.view_layer.objects.active = strokes[0]
    for stroke in strokes[1:]:
        stroke.select_set(True)
    bpy.ops.object.join()
    obj = bpy.context.view_layer.objects.active
    obj.name = name
    obj.data.name = f"{name}Mesh"
    return obj


def create_tube(name, points, radius, collection, mat=MAT_DARK, resolution=4):
    tube_parts = []
    for idx, point in enumerate(points):
        tube_parts.append(create_uv_sphere(f"{name}_Joint_{idx}", point, radius, collection, mat))

    for idx, (start, end) in enumerate(zip(points, points[1:])):
        p0 = Vector(start)
        p1 = Vector(end)
        mid = (p0 + p1) * 0.5
        direction = p1 - p0
        length = direction.length
        if length <= 0:
            continue
        bpy.ops.mesh.primitive_cylinder_add(vertices=32, radius=radius, depth=length, location=mid)
        obj = bpy.context.active_object
        obj.name = f"{name}_Segment_{idx}"
        obj.rotation_euler = direction.to_track_quat("Z", "Y").to_euler()
        assign(obj, mat)
        link_only_to_collection(obj, collection)
        tube_parts.append(obj)

    bpy.ops.object.select_all(action="DESELECT")
    tube_parts[0].select_set(True)
    bpy.context.view_layer.objects.active = tube_parts[0]
    for obj in tube_parts[1:]:
        obj.select_set(True)
    bpy.ops.object.join()
    obj = bpy.context.view_layer.objects.active
    obj.name = name
    obj.data.name = f"{name}Mesh"
    return obj


def add_bevel(obj, width, segments=4, method="ANGLE"):
    mod = obj.modifiers.new(name="Bevel", type="BEVEL")
    mod.width = width
    mod.segments = segments
    mod.limit_method = method
    if method == "ANGLE":
        mod.angle_limit = math.radians(25.0)
    activate(obj)
    bpy.ops.object.modifier_apply(modifier=mod.name)


def shade(obj):
    if obj.type != "MESH":
        return
    for poly in obj.data.polygons:
        poly.use_smooth = True
    if hasattr(obj.data, "set_sharp_from_angle"):
        obj.data.set_sharp_from_angle(angle=math.radians(55.0))


def boolean_apply(target, operand, operation):
    mod = target.modifiers.new(name=f"Bool_{operation}", type="BOOLEAN")
    mod.operation = operation
    mod.solver = "EXACT"
    mod.object = operand
    activate(target)
    bpy.ops.object.modifier_apply(modifier=mod.name)


def subtract_many(base, cutters):
    for obj in cutters:
        boolean_apply(base, obj, "DIFFERENCE")
        bpy.data.objects.remove(obj, do_unlink=True)


def delete_existing_result():
    for obj in list(bpy.data.objects):
        if obj.name == RESULT_NAME or obj.name.startswith(f"{RESULT_NAME}_"):
            bpy.data.objects.remove(obj, do_unlink=True)


def screw(name, x, y, z, radius, depth, collection, cross=True):
    obj = create_cylinder(name, (x, y, z), radius, depth, "Y", collection, MAT_METAL, vertices=36)
    if cross:
        cut_a = create_box(f"{name}_CutA", (x, y - depth * 0.58, z), (radius * 1.45, depth * 0.55, radius * 0.18), collection)
        cut_b = create_box(f"{name}_CutB", (x, y - depth * 0.59, z), (radius * 0.18, depth * 0.56, radius * 1.45), collection)
        subtract_many(obj, [cut_a, cut_b])
    return obj


def radial_ticks(prefix, cx, y, cz, radius, count, length, width, depth, collection, start=0.0, arc=math.tau):
    parts = []
    for i in range(count):
        a = start + arc * i / max(1, count - 1)
        x = cx + math.cos(a) * radius
        z = cz + math.sin(a) * radius
        tick = create_box(
            f"{prefix}_{i}",
            (x, y, z),
            (width, depth, length),
            collection,
            MAT_METAL,
            bevel=width * 0.15,
            segments=2,
        )
        tick.rotation_euler[1] = -a
        parts.append(tick)
    return parts


def knob(prefix, x, y, z, radius, collection, label=None):
    parts = []
    base = create_ring(f"{prefix}_BaseRing", (x, y + wall * 0.08, z), radius * 1.22, radius * 0.80, wall * 0.28, collection, MAT_METAL, 64)
    knob_body = create_cylinder(f"{prefix}_Body", (x, y - wall * 0.38, z), radius, wall * 0.90, "Y", collection, MAT_DARK, vertices=64)
    cap = create_cylinder(f"{prefix}_Cap", (x, y - wall * 0.92, z), radius * 0.62, wall * 0.24, "Y", collection, MAT_METAL, vertices=48)
    parts.extend([base, knob_body, cap])
    for i in range(18):
        a = math.tau * i / 18.0
        rx = x + math.cos(a) * radius * 1.02
        rz = z + math.sin(a) * radius * 1.02
        ridge = create_box(
            f"{prefix}_Grip_{i}",
            (rx, y - wall * 0.43, rz),
            (wall * 0.035, wall * 0.72, wall * 0.15),
            collection,
            MAT_METAL,
            bevel=wall * 0.006,
            segments=1,
        )
        ridge.rotation_euler[1] = -a
        parts.append(ridge)
    parts.extend(radial_ticks(f"{prefix}_ScaleTick", x, y - wall * 1.05, z, radius * 1.55, 13, wall * 0.13, wall * 0.025, wall * 0.10, collection, start=math.radians(35), arc=math.radians(290)))
    if label:
        parts.append(create_text(f"{prefix}_Label", label, (x, y - wall * 1.18, z + radius * 1.85), height * 0.035, collection, MAT_TEXT, extrude=wall * 0.025))
    return parts


source = bpy.data.objects.get(SOURCE_NAME) if SOURCE_NAME else bpy.context.view_layer.objects.active
if source is None or source.type != "MESH":
    raise RuntimeError("No mesh source object found.")

delete_existing_result()
collection = ensure_collection(COLLECTION_NAME)

min_v, max_v = scene_bounds(source)
dims = source.dimensions.copy()

width = dims.x * 1.00
depth = dims.y * 0.88
height = dims.z * 0.99
wall = min(width, depth, height) * 0.055
gap = width * 0.30

center_x = max_v.x + gap + width / 2.0
center_y = (min_v.y + max_v.y) / 2.0
center_z = min_v.z + height / 2.0
front_y = center_y - depth / 2.0
rear_y = center_y + depth / 2.0
right_x = center_x + width / 2.0
left_x = center_x - width / 2.0
top_z = min_v.z + height

parts = []

# Main box: thick hollow case, rounded corners, raised face rails.
body = create_box(RESULT_NAME, (center_x, center_y, center_z), (width, depth, height), collection, MAT_BODY, bevel=wall * 0.55, segments=10)
front_plate = create_box(
    f"{RESULT_NAME}_RaisedFrontPanel",
    (center_x - width * 0.025, front_y - wall * 0.40, center_z),
    (width * 0.91, wall * 0.72, height * 0.89),
    collection,
    MAT_BODY,
    bevel=wall * 0.18,
    segments=5,
)
parts.append(front_plate)

for sx in (-1, 1):
    parts.append(create_cylinder(f"{RESULT_NAME}_FrontCornerPost_{sx}", (center_x + sx * width * 0.485, front_y - wall * 0.52, center_z), wall * 0.24, height * 0.90, "Z", collection, MAT_BODY, vertices=36))
    parts.append(create_box(f"{RESULT_NAME}_FrontSideRail_{sx}", (center_x + sx * width * 0.455, front_y - wall * 0.58, center_z), (wall * 0.30, wall * 0.55, height * 0.83), collection, MAT_BODY, bevel=wall * 0.07, segments=3))

parts.append(create_box(f"{RESULT_NAME}_FrontTopLip", (center_x, front_y - wall * 0.60, min_v.z + height * 0.935), (width * 0.82, wall * 0.48, wall * 0.36), collection, MAT_BODY, bevel=wall * 0.06, segments=3))
parts.append(create_box(f"{RESULT_NAME}_FrontBottomLip", (center_x, front_y - wall * 0.60, min_v.z + height * 0.055), (width * 0.82, wall * 0.52, wall * 0.36), collection, MAT_BODY, bevel=wall * 0.06, segments=3))

# Display assembly: big empty circular screen with layered metal bezel and screws.
screen_x = center_x - width * 0.17
screen_z = min_v.z + height * 0.535
screen_outer = min(width, height) * 0.335
screen_hole = screen_outer * 0.66
screen_mount = create_box(
    f"{RESULT_NAME}_ScreenSquareMount",
    (screen_x, front_y - wall * 0.78, screen_z),
    (screen_outer * 2.22, wall * 0.64, screen_outer * 1.98),
    collection,
    MAT_BODY,
    bevel=wall * 0.12,
    segments=5,
)
parts.append(screen_mount)
outer_ring = create_ring(f"{RESULT_NAME}_OuterScreenBezel", (screen_x, front_y - wall * 1.18, screen_z), screen_outer, screen_hole * 1.08, wall * 0.82, collection, MAT_METAL, 144)
inner_ring = create_ring(f"{RESULT_NAME}_InnerScreenLip", (screen_x, front_y - wall * 1.68, screen_z), screen_hole * 1.10, screen_hole * 0.94, wall * 0.42, collection, MAT_DARK, 144)
parts.extend([outer_ring, inner_ring])
parts.extend(radial_ticks(f"{RESULT_NAME}_ScreenScaleTick", screen_x, front_y - wall * 1.93, screen_z, screen_hole * 1.13, 36, wall * 0.13, wall * 0.018, wall * 0.08, collection))
for i in range(10):
    a = math.tau * i / 10.0 + math.radians(8)
    parts.append(screw(f"{RESULT_NAME}_ScreenScrew_{i}", screen_x + math.cos(a) * screen_outer * 0.89, front_y - wall * 1.90, screen_z + math.sin(a) * screen_outer * 0.89, wall * 0.085, wall * 0.22, collection, cross=False))

# Plates and text blocks.
nameplate_y = front_y - wall * 1.00
top_plate = create_box(f"{RESULT_NAME}_TopIdPlate", (center_x - width * 0.05, nameplate_y, min_v.z + height * 0.835), (width * 0.34, wall * 0.25, height * 0.105), collection, MAT_DARK, bevel=wall * 0.035, segments=2)
bottom_plate = create_box(f"{RESULT_NAME}_BottomIdPlate", (center_x - width * 0.02, nameplate_y, min_v.z + height * 0.075), (width * 0.32, wall * 0.25, height * 0.090), collection, MAT_DARK, bevel=wall * 0.035, segments=2)
parts.extend([top_plate, bottom_plate])
parts.append(create_text(f"{RESULT_NAME}_TopText1", "AN/APS-15", (center_x - width * 0.05, front_y - wall * 1.18, min_v.z + height * 0.865), height * 0.035, collection))
parts.append(create_text(f"{RESULT_NAME}_TopText2", "RANGE 150 NM", (center_x - width * 0.05, front_y - wall * 1.18, min_v.z + height * 0.830), height * 0.024, collection))
parts.append(create_text(f"{RESULT_NAME}_BottomText", "U.S. NAVY", (center_x - width * 0.02, front_y - wall * 1.18, min_v.z + height * 0.083), height * 0.031, collection))
for x, z in [(center_x - width * 0.205, min_v.z + height * 0.87), (center_x + width * 0.105, min_v.z + height * 0.87), (center_x - width * 0.205, min_v.z + height * 0.80), (center_x + width * 0.105, min_v.z + height * 0.80), (center_x - width * 0.175, min_v.z + height * 0.04), (center_x + width * 0.135, min_v.z + height * 0.04)]:
    parts.append(screw(f"{RESULT_NAME}_PlateScrew_{len(parts)}", x, front_y - wall * 1.25, z, wall * 0.055, wall * 0.14, collection, cross=False))

# Right control column and front controls.
right_panel = create_box(
    f"{RESULT_NAME}_RightControlColumn",
    (center_x + width * 0.345, front_y - wall * 0.82, min_v.z + height * 0.535),
    (width * 0.245, wall * 0.86, height * 0.77),
    collection,
    MAT_BODY,
    bevel=wall * 0.10,
    segments=4,
)
parts.append(right_panel)
parts.extend(knob(f"{RESULT_NAME}_IntensityKnob", center_x + width * 0.345, front_y - wall * 1.10, min_v.z + height * 0.710, wall * 0.215, collection, "INTENSITY"))
parts.extend(knob(f"{RESULT_NAME}_FocusKnob", center_x + width * 0.345, front_y - wall * 1.10, min_v.z + height * 0.535, wall * 0.215, collection, "FOCUS"))
parts.extend(knob(f"{RESULT_NAME}_RangeKnob", center_x + width * 0.345, front_y - wall * 1.10, min_v.z + height * 0.325, wall * 0.235, collection, "RANGE"))

for label, lx, lz, size in [
    ("POWER", center_x - width * 0.34, min_v.z + height * 0.225, height * 0.030),
    ("TILT", center_x - width * 0.02, min_v.z + height * 0.195, height * 0.030),
    ("GAIN", center_x + width * 0.210, min_v.z + height * 0.195, height * 0.030),
]:
    parts.append(create_text(f"{RESULT_NAME}_{label}_Label", label, (lx, front_y - wall * 1.12, lz), size, collection))

toggle_base = create_cylinder(f"{RESULT_NAME}_PowerToggleBase", (center_x - width * 0.34, front_y - wall * 1.10, min_v.z + height * 0.150), wall * 0.110, wall * 0.30, "Y", collection, MAT_METAL, vertices=36)
toggle_stem = create_tube(
    f"{RESULT_NAME}_PowerToggleStem",
    [
        (center_x - width * 0.34, front_y - wall * 1.30, min_v.z + height * 0.170),
        (center_x - width * 0.375, front_y - wall * 1.64, min_v.z + height * 0.225),
    ],
    wall * 0.035,
    collection,
    MAT_METAL,
    3,
)
red_light = create_uv_sphere(f"{RESULT_NAME}_RedPowerLamp", (center_x - width * 0.235, front_y - wall * 1.33, min_v.z + height * 0.170), wall * 0.095, collection, MAT_RED)
parts.extend([toggle_base, toggle_stem, red_light])
parts.extend(knob(f"{RESULT_NAME}_TiltKnob", center_x - width * 0.02, front_y - wall * 1.10, min_v.z + height * 0.120, wall * 0.170, collection))
parts.extend(knob(f"{RESULT_NAME}_GainKnob", center_x + width * 0.210, front_y - wall * 1.10, min_v.z + height * 0.120, wall * 0.170, collection))

for sx in (-1, 1):
    for sz in (-1, 1):
        parts.append(screw(f"{RESULT_NAME}_FaceCornerScrew_{sx}_{sz}", center_x + sx * width * 0.435, front_y - wall * 1.05, center_z + sz * height * 0.395, wall * 0.110, wall * 0.25, collection))

# Top lid, handle, hinge bar, and rear/top raised details.
parts.append(create_box(f"{RESULT_NAME}_TopInsetPanel", (center_x, center_y + depth * 0.02, top_z + wall * 0.04), (width * 0.76, depth * 0.67, wall * 0.16), collection, MAT_BODY, bevel=wall * 0.05, segments=3))
parts.append(create_cylinder(f"{RESULT_NAME}_TopRearHingeBar", (center_x, rear_y - depth * 0.055, top_z + wall * 0.19), wall * 0.165, width * 0.72, "X", collection, MAT_METAL, vertices=48))
parts.append(create_cylinder(f"{RESULT_NAME}_TopHandleRod", (center_x, center_y + depth * 0.455, top_z + wall * 0.28), wall * 0.105, width * 0.73, "X", collection, MAT_METAL, vertices=40))
for sx in (-1, 1):
    parts.append(create_box(f"{RESULT_NAME}_TopHandleMount_{sx}", (center_x + sx * width * 0.39, center_y + depth * 0.42, top_z + wall * 0.12), (wall * 0.55, wall * 0.72, wall * 0.55), collection, MAT_BODY, bevel=wall * 0.04, segments=3))

# Right side: louvered vents, side data plate, round connectors, and cable loop.
side_frame = create_box(f"{RESULT_NAME}_SideVentFrame", (right_x + wall * 0.06, center_y + depth * 0.12, min_v.z + height * 0.625), (wall * 0.18, depth * 0.50, height * 0.34), collection, MAT_BODY, bevel=wall * 0.04, segments=2)
side_plate = create_box(f"{RESULT_NAME}_SideDataPlate", (right_x + wall * 0.09, center_y + depth * 0.08, min_v.z + height * 0.375), (wall * 0.19, depth * 0.34, height * 0.22), collection, MAT_DARK, bevel=wall * 0.035, segments=2)
parts.extend([side_frame, side_plate])
parts.append(create_text(f"{RESULT_NAME}_SidePlateText", "IFF-33", (right_x + wall * 0.205, center_y + depth * 0.08, min_v.z + height * 0.405), height * 0.032, collection, MAT_TEXT, extrude=wall * 0.018))
parts[-1].rotation_euler = (math.radians(90.0), 0.0, math.radians(90.0))

for i, z in enumerate((min_v.z + height * 0.205, min_v.z + height * 0.335, min_v.z + height * 0.455), start=1):
    conn_base = create_cylinder(f"{RESULT_NAME}_SideConnectorBase_{i}", (right_x + wall * 0.18, center_y - depth * 0.235, z), wall * 0.165, wall * 0.36, "X", collection, MAT_METAL, vertices=40)
    conn_socket = create_cylinder(f"{RESULT_NAME}_SideConnectorSocket_{i}", (right_x + wall * 0.43, center_y - depth * 0.235, z), wall * 0.105, wall * 0.34, "X", collection, MAT_DARK, vertices=32)
    parts.extend([conn_base, conn_socket])
    parts.append(create_text(f"{RESULT_NAME}_J{i}_Label", f"J{i}", (right_x + wall * 0.19, center_y - depth * 0.390, z + wall * 0.015), height * 0.026, collection, MAT_TEXT, extrude=wall * 0.014))
    parts[-1].rotation_euler = (math.radians(90.0), 0.0, math.radians(90.0))

parts.append(create_tube(
    f"{RESULT_NAME}_SideCableLoop",
    [
        (right_x + wall * 0.55, center_y - depth * 0.235, min_v.z + height * 0.205),
        (right_x + wall * 0.85, center_y - depth * 0.22, min_v.z + height * 0.145),
        (right_x + wall * 0.92, center_y + depth * 0.08, min_v.z + height * 0.095),
        (right_x + wall * 0.58, center_y + depth * 0.35, min_v.z + height * 0.105),
    ],
    wall * 0.055,
    collection,
    MAT_DARK,
    4,
))

# Lower skids.
for sx in (-1, 1):
    parts.append(create_box(f"{RESULT_NAME}_BottomSkidFront_{sx}", (center_x + sx * width * 0.32, front_y + depth * 0.10, min_v.z + wall * 0.16), (width * 0.18, depth * 0.18, wall * 0.30), collection, MAT_BODY, bevel=wall * 0.06, segments=3))
    parts.append(create_box(f"{RESULT_NAME}_BottomSkidRear_{sx}", (center_x + sx * width * 0.32, rear_y - depth * 0.11, min_v.z + wall * 0.16), (width * 0.18, depth * 0.18, wall * 0.30), collection, MAT_BODY, bevel=wall * 0.06, segments=3))

# Real print cuts: hollow body, open display circle, and side ventilation.
cutters = []
cutters.append(create_box(f"{RESULT_NAME}_InnerHollowCavity", (center_x, center_y + wall * 0.06, center_z), (width - wall * 2.15, depth - wall * 2.05, height - wall * 2.15), collection, MAT_DARK))
screen_cut = create_cylinder(f"{RESULT_NAME}_ScreenThroughHole", (screen_x, center_y, screen_z), screen_hole * 0.98, depth + wall * 8.0, "Y", collection, MAT_DARK, vertices=128)
cutters.append(screen_cut)
boolean_apply(front_plate, screen_cut, "DIFFERENCE")
boolean_apply(screen_mount, screen_cut, "DIFFERENCE")

for i in range(6):
    z = min_v.z + height * 0.735 - i * height * 0.055
    cutters.append(create_box(f"{RESULT_NAME}_SideVentSlotCut_{i}", (right_x + wall * 0.03, center_y + depth * 0.12, z), (wall * 1.30, depth * 0.40, wall * 0.20), collection, MAT_DARK, bevel=wall * 0.045, segments=2))

for i, z in enumerate((min_v.z + height * 0.205, min_v.z + height * 0.335, min_v.z + height * 0.455), start=1):
    cutters.append(create_cylinder(f"{RESULT_NAME}_ConnectorHole_{i}", (right_x + wall * 0.20, center_y - depth * 0.235, z), wall * 0.070, wall * 0.65, "X", collection, MAT_DARK, vertices=28))

subtract_many(body, cutters)

final_bevel = body.modifiers.new(name="FinalBodyEdgeSoften", type="BEVEL")
final_bevel.width = wall * 0.035
final_bevel.segments = 2
final_bevel.limit_method = "ANGLE"
activate(body)
bpy.ops.object.modifier_apply(modifier=final_bevel.name)

all_parts = [body] + [obj for obj in parts if obj.name in bpy.data.objects]
for obj in all_parts:
    shade(obj)

bpy.ops.object.select_all(action="DESELECT")
body.select_set(True)
bpy.context.view_layer.objects.active = body
for obj in parts:
    if obj.name in bpy.data.objects:
        obj.select_set(True)
bpy.ops.object.join()
body = bpy.context.view_layer.objects.active
body.name = RESULT_NAME
body.data.name = f"{RESULT_NAME}Mesh"
activate(body)

__result__ = {
    "source_name": source.name,
    "result_name": body.name,
    "dimensions": list(body.dimensions),
    "wall_thickness": wall,
    "screen_hole_radius": screen_hole,
    "collection": collection.name,
    "part_count": len(parts),
}
