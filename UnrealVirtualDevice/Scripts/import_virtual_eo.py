import os
import unreal

fbx_path = os.path.abspath(
    os.path.join(unreal.Paths.project_dir(), "..", "SK_DLS_VirtualEO.fbx")
)
destination_path = "/Game/VirtualEO"
asset_name = "SK_DLS_VirtualEO"
asset_path = f"{destination_path}/{asset_name}"

if not os.path.isfile(fbx_path):
    raise RuntimeError(f"VirtualEO FBX not found: {fbx_path}")

task = unreal.AssetImportTask()
task.set_editor_property("filename", fbx_path)
task.set_editor_property("destination_path", destination_path)
task.set_editor_property("destination_name", asset_name)
task.set_editor_property("automated", True)
task.set_editor_property("replace_existing", True)
task.set_editor_property("save", True)

options = unreal.FbxImportUI()
options.set_editor_property("import_mesh", True)
options.set_editor_property("import_as_skeletal", True)
options.set_editor_property("import_animations", False)
options.set_editor_property("import_materials", True)
options.set_editor_property("import_textures", True)
options.set_editor_property("create_physics_asset", False)
options.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_SKELETAL_MESH)

skeletal_data = options.get_editor_property("skeletal_mesh_import_data")
skeletal_data.set_editor_property("import_morph_targets", False)
skeletal_data.set_editor_property("update_skeleton_reference_pose", False)
skeletal_data.set_editor_property("use_t0_as_ref_pose", False)
task.set_editor_property("options", options)

unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
imported_paths = list(task.get_editor_property("imported_object_paths"))
if not unreal.EditorAssetLibrary.does_asset_exist(asset_path):
    raise RuntimeError(
        f"Skeletal Mesh was not created at {asset_path}; imported={imported_paths}"
    )

asset = unreal.EditorAssetLibrary.load_asset(asset_path)
if not isinstance(asset, unreal.SkeletalMesh):
    raise RuntimeError(f"Imported asset is not a SkeletalMesh: {asset}")

unreal.EditorAssetLibrary.save_directory(destination_path, only_if_is_dirty=False, recursive=True)
unreal.log(
    "VIRTUALEO_IMPORT_SUCCESS "
    f"asset={asset_path} class={asset.get_class().get_name()} imported={imported_paths}"
)
