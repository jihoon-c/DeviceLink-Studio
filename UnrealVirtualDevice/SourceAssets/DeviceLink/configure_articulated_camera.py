import unreal


BASE_PATH = "/Game/DeviceLink/Equipment/Articulated/SM_DLS_FixedDualSensor_Base_Connected.SM_DLS_FixedDualSensor_Base_Connected"
PAN_PATH = "/Game/DeviceLink/Equipment/Articulated/SM_DLS_FixedDualSensor_Pan_Connected.SM_DLS_FixedDualSensor_Pan_Connected"
TILT_PATH = "/Game/DeviceLink/Equipment/Articulated/SM_DLS_FixedDualSensor_Tilt_Connected.SM_DLS_FixedDualSensor_Tilt_Connected"

base_asset = unreal.load_asset(BASE_PATH)
pan_asset = unreal.load_asset(PAN_PATH)
tilt_asset = unreal.load_asset(TILT_PATH)
if not all((base_asset, pan_asset, tilt_asset)):
    raise RuntimeError("CODEX_ARTICULATED_ASSETS_MISSING")

level_subsystem = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
level_subsystem.load_level("/Game/Maps/DeviceLinkLab")
actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
actors = actor_subsystem.get_all_level_actors()
devices = [actor for actor in actors if actor.get_class().get_name() == "VirtualGimbalDevice"]
if not devices:
    raise RuntimeError("CODEX_NO_DEVICE")

device = devices[0]
components = {
    component.get_name(): component
    for component in device.get_components_by_class(unreal.ActorComponent)
}
root = components["SceneRoot"]
base = components["BaseMesh"]
pan_pivot = components["PanPivot"]
pan_mesh = components["PanHousingMesh"]
tilt_pivot = components["TiltPivot"]
sensor = components["SensorMesh"]
gimbal = components["GimbalMesh"]
state = components["DeviceState"]

keep = unreal.AttachmentRule.KEEP_RELATIVE
base.attach_to_component(root, "", keep, keep, keep, False)
pan_pivot.attach_to_component(root, "", keep, keep, keep, False)
pan_mesh.attach_to_component(pan_pivot, "", keep, keep, keep, False)
tilt_pivot.attach_to_component(pan_pivot, "", keep, keep, keep, False)
sensor.attach_to_component(tilt_pivot, "", keep, keep, keep, False)

base.set_editor_property("static_mesh", base_asset)
pan_mesh.set_editor_property("static_mesh", pan_asset)
sensor.set_editor_property("static_mesh", tilt_asset)

base.set_relative_location(unreal.Vector(600.0, 0.0, 0.0), False, False)
base.set_relative_rotation(unreal.Rotator(0.0, 0.0, 0.0), False, False)
base.set_relative_scale3d(unreal.Vector(1.0, 1.0, 1.0))
pan_pivot.set_relative_location(unreal.Vector(600.0, 0.0, 148.0), False, False)
pan_pivot.set_relative_rotation(unreal.Rotator(0.0, 0.0, 0.0), False, False)
pan_mesh.set_relative_location(unreal.Vector(0.0, 0.0, 0.0), False, False)
pan_mesh.set_relative_rotation(unreal.Rotator(0.0, 0.0, 0.0), False, False)
pan_mesh.set_relative_scale3d(unreal.Vector(1.0, 1.0, 1.0))
tilt_pivot.set_relative_location(unreal.Vector(0.0, 0.0, 124.0), False, False)
tilt_pivot.set_relative_rotation(unreal.Rotator(0.0, 0.0, 0.0), False, False)
sensor.set_relative_location(unreal.Vector(0.0, 0.0, 0.0), False, False)
sensor.set_relative_rotation(unreal.Rotator(0.0, 0.0, 0.0), False, False)
sensor.set_relative_scale3d(unreal.Vector(1.0, 1.0, 1.0))

for component in (base, pan_pivot, pan_mesh, tilt_pivot, sensor):
    component.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
    component.set_visibility(True, True)
    component.set_hidden_in_game(False)

if hasattr(gimbal, "set_skinned_asset_and_update"):
    gimbal.set_skinned_asset_and_update(None, True)
else:
    gimbal.set_editor_property("skinned_asset", None)
gimbal.set_visibility(False, True)
gimbal.set_hidden_in_game(True)

state.set_editor_property("minimum_pan_degrees", -170.0)
state.set_editor_property("maximum_pan_degrees", 170.0)
state.set_editor_property("minimum_tilt_degrees", -45.0)
state.set_editor_property("maximum_tilt_degrees", 80.0)

for actor in actors:
    if actor.get_actor_label().startswith("DLS_Fixed_Dual_Sensor"):
        actor.set_actor_hidden_in_game(True)
        if actor.get_class().get_name() == "StaticMeshActor":
            component = actor.get_editor_property("static_mesh_component")
            component.set_visibility(False, True)
            component.set_hidden_in_game(True)

for component in components.values():
    component.modify()
device.modify()
level_subsystem.save_current_level()
saved = unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
print(f"CODEX_ARTICULATED_DONE saved={saved}")
