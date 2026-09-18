# Unreal 가상장비·현장 자산 모델링 사양서

## 0. 공통 제작 규칙

이 문서는 **Unreal 가상 EO/IR 장비 ↔ Windows 연결 프로그램** 통합 데모용 3D 자산 사양이다.
실존 제품을 복제하지 않는 범용 감시 현장 콘셉트만 사용한다.

- Unreal 단위: `1 uu = 1 cm`. 전방 `+X`, 상방 `+Z`.
- FBX 또는 glTF, PBR Metallic/Roughness, 2K 이하 texture, baked lighting 금지.
- mesh: `SM_DLS_<Name>`, material: `M_DLS_<Name>`.
- 상표·로고·읽을 수 있는 텍스트·실제품 고유 형상·무기·인물은 금지한다.
- AI 생성 후 Unreal Editor에서 scale, pivot, collision, LOD를 보정한다.

## 필요한 자산 목록

| 번호 | 자산 | 우선도 | 용도 |
| --- | --- | --- | --- |
| 1 | VirtualEO 가상 EO/IR 짐벌 | 필수 | TCP 명령과 telemetry가 구동하는 핵심 장비 |
| 2 | 감시대 및 장착 pedestal | 필수 | 짐벌 고정 및 현장 실루엣 |
| 3 | 현장 제어함 | 필수 | 통신·전원 인프라 시각화 |
| 4 | 소형 표적 차량 | 필수 | Pan/Tilt·스캔 관측 대상 |
| 5 | 소형 정찰 드론 | 권장 | 상향 Tilt·추적 시연 |
| 6 | 경계 펜스 모듈 | 권장 | 현장 경계 구성 |
| 7 | 조명 폴 | 권장 | 야간·경보 시각 보조 |
| 8 | 관제 건물 모듈 | 선택 | 배경·규모감 |

---

# 1. VirtualEO 가상 EO/IR 짐벌

## 1-1. VirtualEO 모델링 사양

| 항목 | 사양 |
| --- | --- |
| 전체 크기 | W 80 × D 80 × H 150 cm |
| 구성 | Base, PanHousing, TiltYoke, EO Camera, IR Camera, Laser Module, Status Light |
| 가동 범위 | Pan -170°~+170°, Tilt -45°~+80° |
| LOD0 | 전체 15,000~30,000 triangles |
| 재질 | 무광 진회색 도장 금속, 검정 광학 유리, 고무 cable guard, emissive 상태등 |
| collision | Base/Pan/Sensor 단순 box 또는 convex |

**필수 분리 mesh / pivot**

```text
SM_DLS_VirtualEO_Base          바닥 중앙
SM_DLS_VirtualEO_PanHousing    수직 Pan 회전축 중심
SM_DLS_VirtualEO_TiltYoke      수평 Tilt 회전축 중심
SM_DLS_VirtualEO_EOCamera      Tilt 회전축 중심
SM_DLS_VirtualEO_IRCamera      Tilt 회전축 중심
SM_DLS_VirtualEO_LaserModule   Tilt 회전축 중심
SM_DLS_VirtualEO_StatusLight   mesh 중심
```

`AVirtualGimbalDevice`의 `BaseMesh`, `PanPivot`, `PanHousingMesh`, `TiltPivot`,
`SensorMesh`, `StatusLight`에 연결한다. 가동부를 하나의 합쳐진 mesh로 만들면 안 된다.

**AI 모델링 지시문**

```text
Create an original, non-branded fixed surveillance EO/IR gimbal for an Unreal Engine
technical simulation. It is 80 cm wide, 80 cm deep, 150 cm high. Split into separate
objects: heavy square base, cylindrical pan housing, U-shaped tilt yoke, large EO camera
pod, smaller IR camera pod, compact laser rangefinder box, and status beacon. The pan
housing rotates around vertical Z; sensor pods rotate around horizontal Y; front is +X.
Matte charcoal painted metal, black optical glass, rubber cable guards, restrained orange
safety markings. Clean PBR, real-world scale, medium detail, UVs, centered functional
pivots, collision-ready forms. No logo, readable text, weapon, human, background, or baked light.
```

---

# 2. 감시대 및 장비 장착 Pedestal

## 2-1. Pedestal 모델링 사양

| 항목 | 사양 |
| --- | --- |
| 자산명 | `SM_DLS_GimbalPedestal` |
| 크기 | 기초 W 150 × D 150 × H 30 cm, pedestal H 300~500 cm |
| 구조 | 콘크리트 기초, steel column, 평평한 상단 mounting plate |
| LOD0 | 2,000~5,000 triangles |
| 재질 | light concrete, 무광 dark steel, generic bolt |

**AI 모델링 지시문**

```text
Create an original industrial surveillance pedestal for an Unreal Engine virtual EO/IR
gimbal. Square reinforced concrete foundation, 4 meter sturdy steel mast, flat square
mounting plate with generic bolts. Real-world centimeters, +X front. Dark gray steel and
weathered concrete PBR. Keep the top plate flat and centered for a separate gimbal asset.
One static mesh, no logo, text, cable, people, weapon, or baked lighting.
```

---

# 3. 현장 제어함

## 3-1. 제어함 모델링 사양

| 항목 | 사양 |
| --- | --- |
| 자산명 | `SM_DLS_ControlCabinet` |
| 크기 | W 70 × D 45 × H 140 cm |
| 구조 | 잠금식 전면 도어, ventilation grille, cable entry, 상태 LED 3개 |
| LOD0 | 1,500~3,000 triangles |
| 배치 | pedestal에서 2~4 m |

**AI 모델링 지시문**

```text
Create a generic outdoor industrial control cabinet for an Unreal Engine equipment
simulation. Dimensions 70 x 45 x 140 cm. Light gray powder-coated steel box, front door
seams, locked handle, two ventilation grilles, bottom cable conduit entry, three tiny
unlit status lenses. PBR, slightly weathered, single static mesh, bottom-center pivot.
No readable labels, logo, people, exposed electronics, or baked lighting.
```

---

# 4. 소형 표적 차량

## 4-1. 표적 차량 모델링 사양

| 항목 | 사양 |
| --- | --- |
| 자산명 | `SM_DLS_TargetUtilityVehicle` |
| 크기 | W 190 × D 430 × H 180 cm |
| 용도 | 짐벌 Pan/Tilt·스캔 시연용 비무장 관측 대상 |
| LOD0 | 4,000~8,000 triangles |
| 재질 | 먼지 낀 무광 beige/gray body, 검정 tire |

**AI 모델링 지시문**

```text
Create an original unarmed utility target vehicle for an Unreal Engine surveillance
training yard. Compact boxy four-wheel maintenance vehicle, 190 cm wide, 430 cm long,
180 cm high. Enclosed cab with opaque dark windows, rugged tires, simple roof rack.
Dusty matte beige and gray PBR paint. Separate wheel meshes preferred. No brand, plate,
weapon, soldier, readable text, or background.
```

---

# 5. 소형 정찰 드론

## 5-1. 정찰 드론 모델링 사양

| 항목 | 사양 |
| --- | --- |
| 자산명 | `SM_DLS_TargetQuadDrone` |
| 크기 | W 70 × D 70 × H 22 cm |
| 용도 | 상향 Tilt·추적·경보 장면의 공중 표적 |
| LOD0 | 1,000~2,500 triangles |
| pivot | 무게중심 중앙, rotor 분리 선택 |

**AI 모델링 지시문**

```text
Create a compact non-weaponized quadcopter reconnaissance target for an Unreal Engine
EO/IR gimbal training scene. 70 cm square and 22 cm high, four simple arms, protected
rotors, compact central sensor body, tiny navigation-light housings. Matte charcoal
carbon composite PBR, centered pivot. No brand, text, weapon, human, or background.
```

---

# 6. 경계 펜스 모듈

## 6-1. 펜스 모델링 사양

| 항목 | 사양 |
| --- | --- |
| 자산명 | `SM_DLS_PerimeterFence_400` |
| 크기 | W 400 × D 12 × H 220 cm |
| 구조 | welded mesh panel, 양 끝 steel post |
| LOD0 | 500~1,500 triangles |
| 배치 | 양 edge가 4 m snapping 되도록 정렬 |

**AI 모델링 지시문**

```text
Create a modular 4 meter outdoor perimeter fence segment for an Unreal Engine industrial
surveillance yard. 220 cm tall welded steel mesh panel, two square steel posts, subtle
concrete footing. Dark galvanized PBR metal, low-poly efficient geometry, clean snapping
edges. No barbed wire, signage, logo, text, people, or background.
```

---

# 7. 조명 폴

## 7-1. 조명 폴 모델링 사양

| 항목 | 사양 |
| --- | --- |
| 자산명 | `SM_DLS_SiteLightPole` |
| 크기 | H 600 cm, arm 80 cm |
| 구조 | 원형 pole, 상단 LED floodlight 1~2개 |
| LOD0 | 500~1,200 triangles |
| 주의 | 실제 조명은 별도 Unreal Light Actor로 배치 |

**AI 모델링 지시문**

```text
Create a generic 6 meter outdoor industrial light pole for an Unreal Engine simulation
yard. Straight galvanized steel pole, compact rectangular LED floodlight on a short arm,
small bolted base. Low-poly efficient PBR metal and glass. No glow effect, logo, text,
people, wires, or background. One static mesh, ground-level pivot.
```

---

# 8. 관제 건물 모듈

## 8-1. 관제 건물 모델링 사양

| 항목 | 사양 |
| --- | --- |
| 자산명 | `SM_DLS_ControlBuilding` |
| 크기 | W 800 × D 600 × H 350 cm |
| 구조 | 단층 prefab, 평지붕, 출입문, 작은 tinted window, 외부 HVAC box |
| LOD0 | 3,000~8,000 triangles |
| 용도 | 배경용, 내부 구현 불필요 |

**AI 모델링 지시문**

```text
Create a small original single-story industrial control building for an Unreal Engine
virtual surveillance site. 8 m wide, 6 m deep, 3.5 m high. Modular prefab construction,
light concrete panels, flat roof, one dark metal door, two small tinted windows, compact
exterior HVAC box. Exterior only, PBR, simple geometry, ground-level centered pivot.
No signage, brand, human, military marking, or baked sunlight.
```

## 적용 순서와 import 점검

1. `VirtualEO`와 `GimbalPedestal`을 우선 적용한다.
2. `ControlCabinet`, `TargetUtilityVehicle`로 기본 통신·Pan/Tilt 데모를 완성한다.
3. Drone, Fence, LightPole은 스캔·장애·야간 장면을 보강할 때 추가한다.
4. import 뒤 cm scale, pivot, LOD1/2, 단순 collision을 반드시 점검한다.
5. `VirtualGimbal_01`에서 Power, Initialize, Set Pan/Tilt, Start/Stop Scan을 실행해 mesh 가동부가
   코드의 `PanPivot`과 `TiltPivot`에 맞춰 움직이는지 확인한다.

