import unreal


LEVEL_PATH = "/Game/Maps/DeviceLinkLab"
CYLINDER_PATH = "/Engine/BasicShapes/Cylinder.Cylinder"
PEDESTAL_LABEL = "DLS_Fixed_Camera_Pedestal"
PEDESTAL_HEIGHT_CM = 300.0
PEDESTAL_DIAMETER_CM = 220.0

level_subsystem = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
level_subsystem.load_level(LEVEL_PATH)
actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
actors = actor_subsystem.get_all_level_actors()

device = next(
    (
        actor
        for actor in actors
        if actor.get_class().get_name() == "VirtualGimbalDevice"
    ),
    None,
)
if device is None:
    raise RuntimeError("CODEX_FIXED_CAMERA_DEVICE_NOT_FOUND")

components = {
    component.get_name(): component
    for component in device.get_components_by_class(unreal.ActorComponent)
}
base_mesh = components.get("BaseMesh")
if base_mesh is None or base_mesh.get_editor_property("static_mesh") is None:
    raise RuntimeError("CODEX_FIXED_CAMERA_BASE_NOT_CONFIGURED")

cylinder_mesh = unreal.load_asset(CYLINDER_PATH)
if cylinder_mesh is None:
    raise RuntimeError("CODEX_ENGINE_CYLINDER_NOT_FOUND")

device_location = device.get_actor_location()
base_relative_location = base_mesh.get_editor_property("relative_location")
base_location = unreal.Vector(
    device_location.x + base_relative_location.x,
    device_location.y + base_relative_location.y,
    device_location.z + base_relative_location.z,
)
pedestal_location = unreal.Vector(
    base_location.x,
    base_location.y,
    base_location.z - (PEDESTAL_HEIGHT_CM * 0.5),
)

pedestal = next(
    (actor for actor in actors if actor.get_actor_label() == PEDESTAL_LABEL),
    None,
)
if pedestal is None:
    pedestal = actor_subsystem.spawn_actor_from_class(
        unreal.StaticMeshActor,
        pedestal_location,
        unreal.Rotator(pitch=0.0, yaw=0.0, roll=0.0),
        transient=False,
    )
    if pedestal is None:
        raise RuntimeError("CODEX_PEDESTAL_SPAWN_FAILED")

pedestal.set_actor_label(PEDESTAL_LABEL)
pedestal.set_folder_path("DeviceLink/Equipment/FixedCamera")
pedestal.set_editor_property("is_spatially_loaded", False)
pedestal.set_actor_location(pedestal_location, False, False)
pedestal.set_actor_rotation(
    unreal.Rotator(pitch=0.0, yaw=0.0, roll=0.0), False
)
pedestal.set_actor_scale3d(
    unreal.Vector(
        PEDESTAL_DIAMETER_CM / 100.0,
        PEDESTAL_DIAMETER_CM / 100.0,
        PEDESTAL_HEIGHT_CM / 100.0,
    )
)
pedestal.set_actor_hidden_in_game(False)

mesh_component = pedestal.get_editor_property("static_mesh_component")
mesh_component.set_editor_property("static_mesh", cylinder_mesh)
mesh_component.set_editor_property("mobility", unreal.ComponentMobility.STATIC)
mesh_component.set_visibility(True, True)
mesh_component.set_hidden_in_game(False)
mesh_component.set_collision_enabled(unreal.CollisionEnabled.QUERY_AND_PHYSICS)

pedestal.modify()
mesh_component.modify()
level_subsystem.save_current_level()
saved = unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
raise RuntimeError(
    "CODEX_PEDESTAL_DONE "
    f"location={pedestal_location} "
    f"scale={pedestal.get_actor_scale3d()} saved={saved}"
)
