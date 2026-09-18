import unreal


asset_path = "/Game/VirtualEO/SK_DLS_VirtualEO"
required_bones = {"root", "gimbal_yaw", "gimbal_roll", "gimbal_pitch"}

mesh = unreal.EditorAssetLibrary.load_asset(asset_path)
if not isinstance(mesh, unreal.SkeletalMesh):
    raise RuntimeError(f"VirtualEO Skeletal Mesh is missing: {asset_path}")

actor_class = unreal.load_class(None, "/Script/UnrealVirtualDevice.VirtualGimbalDevice")
if actor_class is None:
    raise RuntimeError("AVirtualGimbalDevice class could not be loaded")

actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
level_subsystem = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if not level_subsystem.load_level("/Game/Maps/DeviceLinkLab"):
    raise RuntimeError("DeviceLinkLab level could not be loaded")

placed_actors = [
    level_actor
    for level_actor in actor_subsystem.get_all_level_actors()
    if level_actor.get_class() == actor_class
]
if len(placed_actors) != 1:
    raise RuntimeError(
        "DeviceLinkLab must contain exactly one AVirtualGimbalDevice; "
        f"found {len(placed_actors)}"
    )

actor = actor_subsystem.spawn_actor_from_class(
    actor_class, unreal.Vector(0.0, 0.0, 0.0)
)
if actor is None:
    raise RuntimeError("AVirtualGimbalDevice could not be spawned")

try:
    components = actor.get_components_by_class(unreal.PoseableMeshComponent)
    if len(components) != 1:
        raise RuntimeError(
            f"Expected one PoseableMeshComponent, found {len(components)}"
        )

    component = components[0]
    bone_indices = {
        bone_name: component.get_bone_index(bone_name)
        for bone_name in sorted(required_bones)
    }
    missing_bones = sorted(
        bone_name for bone_name, bone_index in bone_indices.items() if bone_index < 0
    )
    if missing_bones:
        raise RuntimeError(
            f"VirtualEO rig is missing required bones: {missing_bones}; "
            f"indices={bone_indices}"
        )

    unreal.log(
        "VIRTUALEO_VERIFY_SUCCESS "
        f"asset={asset_path} bone_indices={bone_indices} "
        f"placed_actor={placed_actors[0].get_name()} actor={actor.get_name()}"
    )
finally:
    actor_subsystem.destroy_actor(actor)
