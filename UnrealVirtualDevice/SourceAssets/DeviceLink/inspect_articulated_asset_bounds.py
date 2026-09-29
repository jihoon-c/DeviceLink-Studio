import unreal


ASSETS = (
    "/Game/DeviceLink/Equipment/Articulated/SM_DLS_FixedDualSensor_Base_Connected.SM_DLS_FixedDualSensor_Base_Connected",
    "/Game/DeviceLink/Equipment/Articulated/SM_DLS_FixedDualSensor_Pan_Connected.SM_DLS_FixedDualSensor_Pan_Connected",
    "/Game/DeviceLink/Equipment/Articulated/SM_DLS_FixedDualSensor_Tilt_Connected.SM_DLS_FixedDualSensor_Tilt_Connected",
)

for asset_path in ASSETS:
    asset = unreal.load_asset(asset_path)
    if asset is None:
        print(f"DLS_BOUNDS missing={asset_path}")
        continue
    bounds = asset.get_bounds()
    materials = [
        static_material.get_editor_property("material_interface").get_path_name()
        if static_material.get_editor_property("material_interface") is not None
        else "None"
        for static_material in asset.get_editor_property("static_materials")
    ]
    print(
        "DLS_BOUNDS "
        f"asset={asset.get_name()} "
        f"origin={bounds.origin} "
        f"extent={bounds.box_extent} "
        f"radius={bounds.sphere_radius} "
        f"materials={materials}"
    )
