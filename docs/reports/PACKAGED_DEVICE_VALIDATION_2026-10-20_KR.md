# 최소 가상장비 패키지 검증

## 조건

- Windows x64 / Unreal 5.8 / Development, 최소 맵 `/Game/Maps/DeviceLinkMinimal`
- 기본 명령: `scripts/Package-VirtualDevice.ps1`, cook의 기본 맵 자동 수집 제외
- 운용 확인: 프로젝트 venv Python으로 `mcp_server/demo_launcher.py --smoke-basic`
- 백그라운드 `-nullrhi`, MFC `--demo-hidden`; 화면 자동 조작은 수행하지 않음

## 결과

| 항목 | 결과 |
| --- | --- |
| BuildCookRun | 성공, 종료 코드 0, 최종 시도 약 74초 |
| 실제 실행 경로 | `artifacts/package/Windows/UnrealVirtualDevice.exe` |
| 편집기 실행 | 없음 |
| MFC 자동 연결·첫 Telemetry | 성공 |
| 전원·초기화 | Ready / None 관측 |
| MotorStall 주입·매뉴얼 근거·보고서 | 성공 |
| 복구 전 검증 | FAIL |
| 명시적 고장 해제 후 검증 | PASS |
| LLM | `skipped_disabled`, 시작 결과의 model=null |
| 종료 | cleanup_errors=[], 소유한 MFC·가상장비 PID 종료 확인 |
| 컨테이너 | 최소 맵 1건, DeviceLinkLab·Factory_Pack·SD_Art 0건 |
| 자동 회귀 | C++ 6/6, Python 49/49 |

준비 시작 → MFC 연결 완료는 3.573초였다. 캐시가 있는 해당 PC의 단일 실행값이며 3D 최초 준비 시간은 아니다.

배포 폴더는 63파일 / 1,067,494,195bytes (약 1,018MiB)였다. 여기에는 Development PDB 368,365,568bytes가 포함된다. 장비 컨테이너 `.ucas`는 241,522,304bytes였다. 참조 조사에서 측정한 프로젝트 자산 약 5.7MiB와 배포 크기는 다른 값이다.

## 근거 파일 (Git 제외)

- `artifacts/package-run.log`: 최종 패키징 로그
- `artifacts/asset-audit/packaged-content.csv`: 실제 IoStore 컨테이너 목록
- `artifacts/demo/smoke-basic-result.json`: Ready·모델 미호출·FAIL→PASS·보고서 경로
- `artifacts/demo/launcher-state.json`: 종료 결과
- `artifacts/demo/unreal.log`: 실제 최소 맵 로딩·통신 로그
- `artifacts/asset-audit/Maps_DeviceLinkLab/`, `Maps_DeviceLinkMinimal/`: 참조 조사

## 남은 검증

이 시험의 고장 진단은 브리지 경로를 사용한다. MFC worker→Python의 보고서 경로는 별도의 실제 프로세스 통합 시험으로 확인했으며, MFC 실화면에서 AI 응답 완료를 확인한 것은 아니다. 3D 화면·카메라·고장 표시, 각도·통신 장애 시나리오, DPI·키보드 탐색, 배포 환경의 선택 AI 검증은 남아 있다. 원본 자산은 삭제하지 않았다.
