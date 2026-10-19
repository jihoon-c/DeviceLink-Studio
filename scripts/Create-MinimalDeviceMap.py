"""Generate a separate demonstration map; refuse to overwrite any existing map."""
from pathlib import Path
import unreal


def main():
    destination = '/Game/Maps/DeviceLinkMinimal'
    disk_path = Path(unreal.Paths.project_content_dir()) / 'Maps/DeviceLinkMinimal.umap'
    if disk_path.exists():
        raise RuntimeError('Minimal map already exists; inspect it before regenerating.')
    world = unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    device_class = unreal.load_class(None, '/Script/UnrealVirtualDevice.VirtualGimbalDevice')
    if not device_class:
        raise RuntimeError('Build the UnrealVirtualDevice editor module first.')
    device = actors.spawn_actor_from_class(device_class, unreal.Vector(0, 0, 100))
    device.set_actor_label('DeviceLink Equipment')
    cube = unreal.load_asset('/Engine/BasicShapes/Cube')
    # Floor and three simple targets make pan/tilt visible in the sensor view.
    for label, location, scale in [
        ('Floor', (0, 0, -10), (30, 30, 0.2)),
        ('Target Left', (800, -400, 100), (1, 1, 2)),
        ('Target Center', (900, 0, 150), (1, 1, 3)),
        ('Target Right', (800, 400, 200), (1, 1, 4))]:
        actor = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(*location))
        actor.set_actor_label(label)
        actor.static_mesh_component.set_static_mesh(cube)
        actor.set_actor_scale3d(unreal.Vector(*scale))
    actors.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 700),
                                 unreal.Rotator(-45, -30, 0))
    actors.spawn_actor_from_class(unreal.SkyLight, unreal.Vector(0, 0, 700))
    actors.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(-500, 0, 300))
    if not unreal.EditorLoadingAndSavingUtils.save_map(world, destination):
        raise RuntimeError('Minimal map could not be saved')
    unreal.log('DeviceLink minimal map saved: ' + destination)


if __name__ == '__main__':
    main()
