import unreal


level_subsystem = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
level_subsystem.load_level("/Game/Maps/DeviceLinkLab")
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
device = next(
    actor for actor in actors if actor.get_class().get_name() == "VirtualGimbalDevice"
)
components = {
    component.get_name(): component
    for component in device.get_components_by_class(unreal.ActorComponent)
}
base = components["BaseMesh"]
pan_pivot = components["PanPivot"]
pan_mesh = components["PanHousingMesh"]
tilt_pivot = components["TiltPivot"]
sensor = components["SensorMesh"]
gimbal = components["GimbalMesh"]
state = components["DeviceState"]

hierarchy = {
    "base": base.get_attach_parent().get_name(),
    "pan_pivot": pan_pivot.get_attach_parent().get_name(),
    "pan_mesh": pan_mesh.get_attach_parent().get_name(),
    "tilt_pivot": tilt_pivot.get_attach_parent().get_name(),
    "sensor": sensor.get_attach_parent().get_name(),
}
expected_hierarchy = {
    "base": "SceneRoot",
    "pan_pivot": "SceneRoot",
    "pan_mesh": "PanPivot",
    "tilt_pivot": "PanPivot",
    "sensor": "TiltPivot",
}
if hierarchy != expected_hierarchy:
    raise RuntimeError(f"CODEX_VERIFY_HIERARCHY_FAILED {hierarchy}")

assets = {
    "base": base.get_editor_property("static_mesh").get_path_name(),
    "pan": pan_mesh.get_editor_property("static_mesh").get_path_name(),
    "tilt": sensor.get_editor_property("static_mesh").get_path_name(),
}
expected_asset_suffixes = {
    "base": "SM_DLS_FixedDualSensor_Base_Connected.SM_DLS_FixedDualSensor_Base_Connected",
    "pan": "SM_DLS_FixedDualSensor_Pan_Connected.SM_DLS_FixedDualSensor_Pan_Connected",
    "tilt": "SM_DLS_FixedDualSensor_Tilt_Connected.SM_DLS_FixedDualSensor_Tilt_Connected",
}
for key, suffix in expected_asset_suffixes.items():
    if not assets[key].endswith(suffix):
        raise RuntimeError(f"CODEX_VERIFY_ASSET_FAILED {key}={assets[key]}")

limits = {
    "pan_min": state.get_editor_property("minimum_pan_degrees"),
    "pan_max": state.get_editor_property("maximum_pan_degrees"),
    "tilt_min": state.get_editor_property("minimum_tilt_degrees"),
    "tilt_max": state.get_editor_property("maximum_tilt_degrees"),
}
expected_limits = {
    "pan_min": -170.0,
    "pan_max": 170.0,
    "tilt_min": -45.0,
    "tilt_max": 80.0,
}
if any(abs(limits[key] - expected_limits[key]) > 0.01 for key in limits):
    raise RuntimeError(f"CODEX_VERIFY_LIMIT_FAILED {limits}")

pan_pivot.set_relative_rotation(
    unreal.Rotator(pitch=0.0, yaw=170.0, roll=0.0), False, False
)
tilt_pivot.set_relative_rotation(
    unreal.Rotator(pitch=0.0, yaw=0.0, roll=80.0), False, False
)
positive = {
    "pan": pan_pivot.get_editor_property("relative_rotation").yaw,
    "tilt": tilt_pivot.get_editor_property("relative_rotation").roll,
}
pan_pivot.set_relative_rotation(
    unreal.Rotator(pitch=0.0, yaw=-170.0, roll=0.0), False, False
)
tilt_pivot.set_relative_rotation(
    unreal.Rotator(pitch=0.0, yaw=0.0, roll=-45.0), False, False
)
negative = {
    "pan": pan_pivot.get_editor_property("relative_rotation").yaw,
    "tilt": tilt_pivot.get_editor_property("relative_rotation").roll,
}
pan_pivot.set_relative_rotation(
    unreal.Rotator(pitch=0.0, yaw=0.0, roll=0.0), False, False
)
tilt_pivot.set_relative_rotation(
    unreal.Rotator(pitch=0.0, yaw=0.0, roll=0.0), False, False
)

if abs(positive["pan"] - 170.0) > 0.01 or abs(positive["tilt"] - 80.0) > 0.01:
    raise RuntimeError(f"CODEX_VERIFY_POSITIVE_ROTATION_FAILED {positive}")
if abs(negative["pan"] + 170.0) > 0.01 or abs(negative["tilt"] + 45.0) > 0.01:
    raise RuntimeError(f"CODEX_VERIFY_NEGATIVE_ROTATION_FAILED {negative}")

skinned_asset = gimbal.get_editor_property("skinned_asset")
if skinned_asset is not None:
    raise RuntimeError(f"CODEX_VERIFY_GIMBAL_FALLBACK_FAILED {skinned_asset}")

unreal.log_warning(
    "CODEX_VERIFY_OK "
    f"hierarchy={hierarchy} assets={assets} limits={limits} "
    f"positive={positive} negative={negative}"
)
