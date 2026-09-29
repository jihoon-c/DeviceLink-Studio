import bpy
from mathutils import Vector
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
EXPORT_DIRECTORY = ROOT / "UnrealVirtualDevice" / "SourceAssets" / "DeviceLink" / "Articulated"

BASE_NAMES = {
    "Base_Flange",
    "Base_Riser",
    "Upper_Ring",
}
PAN_NAMES = {
    "Azimuth_Collar",
    "Center_Housing",
    "Center_Top_Cap",
    "Center_Back_Plate",
    "Emblem_2.52",
    "Emblem_2.28",
    "ID_Plate",
}
TILT_NAMES = {
    "SupportArm_L",
    "SupportArm_R",
    "Pivot_L",
    "Pivot_R",
    "Cable_Left",
    "Cable_Right",
    "Warning_Label",
}

BASE_NAMES.update(name for name in bpy.data.objects.keys() if name.startswith("BaseBolt_"))
TILT_NAMES.update(name for name in bpy.data.objects.keys() if name.startswith("EOIR_"))
TILT_NAMES.update(name for name in bpy.data.objects.keys() if name.startswith("MS_"))


def build_export_mesh(name: str, source_names: set[str], pivot: Vector) -> bpy.types.Object:
    old_object = bpy.data.objects.get(name)
    if old_object is not None:
        bpy.data.objects.remove(old_object, do_unlink=True)

    missing = sorted(source_name for source_name in source_names if bpy.data.objects.get(source_name) is None)
    if missing:
        raise RuntimeError(f"Missing source objects for {name}: {missing}")

    collection = bpy.data.collections.new(f"{name}_Build")
    bpy.context.scene.collection.children.link(collection)
    duplicates = []
    for source_name in sorted(source_names):
        source = bpy.data.objects[source_name]
        duplicate = source.copy()
        if source.data is not None:
            duplicate.data = source.data.copy()
        collection.objects.link(duplicate)
        duplicates.append(duplicate)

    bpy.ops.object.select_all(action="DESELECT")
    for duplicate in duplicates:
        duplicate.select_set(True)
    bpy.context.view_layer.objects.active = duplicates[0]
    bpy.ops.object.convert(target="MESH")
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    bpy.context.view_layer.objects.active = duplicates[0]
    bpy.ops.object.join()

    joined = bpy.context.view_layer.objects.active
    joined.name = name
    joined.data.name = f"{name}_Mesh"

    # Bake the functional rotation center into the mesh. The exported object
    # itself stays at zero so Unreal imports a local-space pivot instead of
    # retaining the original scene's absolute height a second time.
    for vertex in joined.data.vertices:
        vertex.co -= pivot
    joined.location = Vector((0.0, 0.0, 0.0))
    joined.rotation_euler = (0.0, 0.0, 0.0)
    joined.scale = (1.0, 1.0, 1.0)

    for child_collection in tuple(joined.users_collection):
        child_collection.objects.unlink(joined)
    bpy.context.scene.collection.objects.link(joined)
    bpy.data.collections.remove(collection)
    return joined


def export_fbx(export_object: bpy.types.Object) -> None:
    bpy.ops.object.select_all(action="DESELECT")
    export_object.select_set(True)
    bpy.context.view_layer.objects.active = export_object
    output_path = EXPORT_DIRECTORY / f"{export_object.name}.fbx"
    bpy.ops.export_scene.fbx(
        filepath=str(output_path),
        use_selection=True,
        object_types={"MESH"},
        apply_unit_scale=True,
        apply_scale_options="FBX_SCALE_UNITS",
        axis_forward="-Y",
        axis_up="Z",
        bake_space_transform=False,
        use_mesh_modifiers=True,
        mesh_smooth_type="FACE",
        add_leaf_bones=False,
    )
    print(f"DLS_EXPORTED {export_object.name} -> {output_path}")


EXPORT_DIRECTORY.mkdir(parents=True, exist_ok=True)
base = build_export_mesh("SM_DLS_FixedDualSensor_Base", BASE_NAMES, Vector((0.0, 0.0, 0.0)))
pan = build_export_mesh("SM_DLS_FixedDualSensor_Pan", PAN_NAMES, Vector((0.0, 0.0, 1.48)))
tilt = build_export_mesh("SM_DLS_FixedDualSensor_Tilt", TILT_NAMES, Vector((0.0, 0.0, 2.72)))

for export_mesh in (base, pan, tilt):
    export_fbx(export_mesh)

bpy.ops.wm.save_as_mainfile(filepath=str(ROOT / "Fixed_Dual_Sensor_Camera.blend"))
print("DLS_REBUILD_COMPLETE")
