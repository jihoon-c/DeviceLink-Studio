# VirtualEO Unreal skeletal rig

## Coordinate convention

- Front: `+X`
- Up: `+Z`
- Real-world scale: metres in Blender, centimetres in Unreal (`100 cm = 1 m`)

## Bone hierarchy

```text
root
└─ gimbal_yaw      Z-axis, -150° to +150°
   └─ gimbal_roll  X-axis, -60° to +60°
      └─ gimbal_pitch  Y-axis, -60° to +120°
```

The Blender bones point along their physical rotation axes, so each mechanism is controlled around the bone's local Y rotation channel. Rotation-limit constraints and equivalent custom properties are stored on the pose bones.

## Rigid mesh assignment

| Bone | Meshes |
|---|---|
| `root` | `SM_DLS_VirtualEO_Base`, `SM_DLS_VirtualEO_StatusLight`, `DLS_DatumTriangle` |
| `gimbal_yaw` | `SM_DLS_VirtualEO_PanHousing`, `SM_DLS_VirtualEO_RearArm` |
| `gimbal_roll` | `SM_DLS_VirtualEO_TiltYoke` |
| `gimbal_pitch` | spherical sensor body, flat panel and insert, EO/IR/laser modules and receiver |

Every mesh vertex has one rigid weight of `1.0`; no blended skinning is used. The Blender helper empties remain for inspection but are marked `export_helper_only`.

## Unreal usage

Import the Armature and all mesh objects together as one Skeletal Mesh. Drive `gimbal_yaw`, `gimbal_roll`, and `gimbal_pitch` in local/component bone space from a Control Rig or an Animation Blueprint. Clamp input angles to the ranges above. Keep `root` fixed to the actor transform.

Recommended component mapping:

```text
AVirtualGimbalDevice
└─ SkeletalMeshComponent: SK_DLS_VirtualEO
   ├─ gimbal_yaw
   ├─ gimbal_roll
   └─ gimbal_pitch
```
