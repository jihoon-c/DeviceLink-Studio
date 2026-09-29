from pathlib import Path

import unreal


SOURCE_DIRECTORY = (
    Path(unreal.Paths.project_dir()) / "SourceAssets" / "DeviceLink" / "Articulated"
)
DESTINATION_PATH = "/Game/DeviceLink/Equipment/Articulated"
ASSET_NAMES = (
    "SM_DLS_FixedDualSensor_Base",
    "SM_DLS_FixedDualSensor_Pan",
    "SM_DLS_FixedDualSensor_Tilt",
)

tasks = []
for asset_name in ASSET_NAMES:
    options = unreal.FbxImportUI()
    options.set_editor_property("import_mesh", True)
    options.set_editor_property("import_as_skeletal", False)
    options.set_editor_property("import_materials", False)
    options.set_editor_property("import_textures", False)
    options.get_editor_property("static_mesh_import_data").set_editor_property(
        "combine_meshes", True
    )

    task = unreal.AssetImportTask()
    task.set_editor_property("filename", str(SOURCE_DIRECTORY / f"{asset_name}.fbx"))
    task.set_editor_property("destination_path", DESTINATION_PATH)
    task.set_editor_property("destination_name", f"{asset_name}_Connected")
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("replace_existing_settings", False)
    task.set_editor_property("save", True)
    task.set_editor_property("options", options)
    tasks.append(task)

unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)

for asset_name in ASSET_NAMES:
    asset_path = f"{DESTINATION_PATH}/{asset_name}_Connected"
    asset = unreal.load_asset(asset_path)
    if asset is None:
        raise RuntimeError(f"DLS_REIMPORT_MISSING asset={asset_path}")
    for material_index, static_material in enumerate(
        asset.get_editor_property("static_materials")
    ):
        slot_name = str(static_material.get_editor_property("material_slot_name"))
        material = unreal.load_asset(f"{DESTINATION_PATH}/{slot_name}")
        if material is not None:
            asset.set_material(material_index, material)
    unreal.EditorAssetLibrary.save_asset(asset_path, only_if_is_dirty=False)
    bounds = asset.get_bounds()
    print(
        "DLS_REIMPORT_OK "
        f"asset={asset.get_name()} "
        f"origin={bounds.origin} "
        f"extent={bounds.box_extent}"
    )

print("DLS_REIMPORT_COMPLETE")
