from pathlib import Path

import bpy
from mathutils import Vector
from math import radians


ROOT = Path(__file__).resolve().parents[3]
OUTPUT_DIRECTORY = ROOT / "artifacts"
OUTPUT_DIRECTORY.mkdir(parents=True, exist_ok=True)

base = bpy.data.objects["SM_DLS_FixedDualSensor_Base"]
pan = bpy.data.objects["SM_DLS_FixedDualSensor_Pan"]
tilt = bpy.data.objects["SM_DLS_FixedDualSensor_Tilt"]
visible_objects = {base.name, pan.name, tilt.name, "Presentation_Ground"}

for scene_object in bpy.context.scene.objects:
    if scene_object.type in {"MESH", "CURVE"}:
        scene_object.hide_render = scene_object.name not in visible_objects

base.location = Vector((0.0, 0.0, 0.0))
pan.location = Vector((0.0, 0.0, 1.48))
tilt.location = Vector((0.0, 0.0, 2.72))

scene = bpy.context.scene
scene.render.resolution_x = 960
scene.render.resolution_y = 720
scene.render.resolution_percentage = 100
scene.render.image_settings.file_format = "PNG"

tilt.rotation_euler = (0.0, 0.0, 0.0)
scene.render.filepath = str(OUTPUT_DIRECTORY / "fixed-camera-connected-neutral.png")
bpy.ops.render.render(write_still=True)

tilt.rotation_euler = (radians(25.0), 0.0, 0.0)
scene.render.filepath = str(OUTPUT_DIRECTORY / "fixed-camera-connected-tilt25.png")
bpy.ops.render.render(write_still=True)

print("DLS_VALIDATION_RENDER_COMPLETE")
