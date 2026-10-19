# Unreal 자산 조사와 경량 배포

## 현재 확인한 결과

2026-10-19, Unreal 5.8 Asset Registry의 하드·소프트·관리 참조와 C++에 명시된 `/Game/` 경로를 조사했다. World Partition 외부 액터·객체도 시작 패키지에 포함했다.

| 대상 | 기존 DeviceLinkLab | 별도 DeviceLinkMinimal |
| --- | ---: | ---: |
| Factory_Pack_V1 참조 크기 | 2,033.60MiB | 0MiB |
| SD_Art 참조 크기 | 0MiB | 0MiB |
| VirtualEO 참조 크기 | 4.19MiB | 4.19MiB |
| DeviceLink 참조 크기 | 2.74MiB | 1.49MiB |
| 외부 액터·객체 참조 크기 | 약 28.51MiB | 0MiB |
| 누락 패키지 | 2건 | 0건 |

기존 맵의 누락 참조:

- `/Game/Factory_Pack_V1/Textures/Texture/T_G_a_s`
- `/Game/Factory_Pack_V1/Textures/Texture/T_power_box`

최소 맵은 장비·바닥·표적·조명으로 구성하며 원본 맵과 기본 시작 맵 설정을 변경하지 않는다. 참조된 프로젝트 자산은 약 5.7MiB이나 엔진 자산·런타임을 포함한 **배포 용량 측정값은 아니다**. 문자열을 조합해 로드하는 경로는 정적 조사에서 빠질 수 있으므로, 미도달 자산을 삭제 가능하다고 판정하지 않는다. 원본 배경 자산은 보존한다.

## 실행

```powershell
# 최초 한 번: 별도 맵 생성. 기존 파일이 있으면 덮어쓰지 않고 실패한다.
.\scripts\Create-MinimalDeviceMap.ps1

# 읽기 전용 조사. 맵별 JSON/Markdown과 로그가 artifacts/asset-audit/에 생성된다.
.\scripts\Audit-UnrealAssets.ps1
.\scripts\Audit-UnrealAssets.ps1 -Map /Game/Maps/DeviceLinkMinimal

# 최소 맵 지정 Windows Development 패키징
.\scripts\Package-VirtualDevice.ps1
```

각 스크립트의 `-EngineRoot`로 엔진 설치 위치를 지정한다. 에디터 모듈을 먼저 빌드해야 한다. 조사·맵 생성에서 PythonScriptPlugin을 해당 실행에만 활성화하고 불필요한 AllToolsets·ModelContextProtocol을 제외한다. 프로젝트 플러그인 설정은 저장하지 않는다.

패키징은 `artifacts/package/Windows/UnrealVirtualDevice.exe`와 맵 이름을 기록한 `devicelink-package.json`을 생성한다. 런처는 패키지를 우선하고 없으면 기존 편집기 경로로 실행한다. 별도 위치는 `DEVICELINK_UNREAL_PACKAGE`로 지정하며, 잘못 지정한 경로는 오류로 안내한다.

cook에는 `-NoDefaultMaps`를 전달해 원본 공장 맵의 자동 수집을 제외한다. `-AdditionalCookerOptions`로 에디터 도구 플러그인도 제외한다. 첫 두 시도의 로그는 `artifacts/package-first-attempt.log`, `package-second-attempt.log`에 보존했다.

## 완료 판정

2026-10-20: Windows Development 패키징 성공. 실제 배포 EXE와 숨김 MFC에서 첫 Telemetry·전원·초기화·MotorStall 진단·복구 FAIL→PASS·종료를 확인했다. 모델은 호출하지 않았다. 컨테이너 목록에서 최소 맵 포함, 기존 공장 맵·Factory_Pack·SD_Art 제외를 확인했다.

- 배포 폴더: 1,067,494,195bytes (약 1,018MiB), PDB 368,365,568bytes 포함
- 기본 준비 시간: 3.573초 (해당 PC 단일 실행, 백그라운드 `-nullrhi`, 캐시가 있는 상태)
- 상세 기록: [배포 검증](reports/PACKAGED_DEVICE_VALIDATION_2026-10-20_KR.md)

전체 기능 검증은 진행 중이다. 실제 3D 화면·카메라·고장 표시, 각도·통신 장애 시나리오, 선택 AI를 배포 환경에서 추가 확인해야 한다. 원본 대비 시작 시간은 동일 조건으로 측정하지 않아 개선 비율을 제시하지 않는다.
