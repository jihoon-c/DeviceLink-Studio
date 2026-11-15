# Windows 배포 후보 검증

소스는 `620efb0`의 추적 파일을 새 폴더에 풀고 이번 런처·로컬 AI 수정을 적용했다. Unreal Engine 5.8의 `DeviceLinkMinimal`을 Win64 Development로 패키징했다. IoStore 목록에서 최소 맵을 확인했으며 `DeviceLinkLab`, `Factory_Pack`, `SD_Art`는 포함되지 않았다.

| 검증 | 결과 |
| --- | --- |
| 새 Python 가상환경의 의존성 설치 | 성공 |
| MFC Release 빌드 및 CTest | 7/7 통과 |
| Python 회귀 시험 | 50/50 통과. SDK의 실제 루프백 HTTP 호출 포함 |
| 배포용 Unreal과 MFC의 기본 스모크 시험 | Ready → MotorStall → 복구 전 FAIL → 고장 해제 후 PASS |
| 최종 ZIP 재설치 | 새 폴더와 새 가상환경에서 런처 사전 검사와 기본 스모크 시험 성공. 편집기 경로는 존재하지 않는 위치로 지정 |
| 기본 모드 종료 직후 3D 모드 시작 | 새 Unreal 프로세스 실행, MFC 자동 연결, 두 세션의 종료 및 TCP 5000 해제 확인 |
| 최종 ZIP의 MFC·Unreal 실화면 | MFC에서 `45° / 10°`를 적용하고 실제·목표 텔레메트리의 일치를 확인. 중앙 정렬 후 실제 값이 `0° / 0°`로 돌아왔고 Unreal 센서 영상에서 표적이 왼쪽 가장자리에서 화면 중앙으로 이동 |
| MFC 실화면의 고장 진단·복구 | Motor Stall 주입 → Fault 표시 → 매뉴얼 인용 보고서 생성 → 해제 전 FAIL → 해제 후 Ready / None 및 PASS. 같은 보고서에 두 판정이 기록됐고 MFC의 최근 보고서 버튼으로 열림 |

Windows ZIP에는 MFC 실행 파일, Python 진단 코드와 설치 요구 사항, 시나리오, 샘플 매뉴얼, 최소 Unreal 패키지, `START_HERE_KR.md`가 들어 있다. Unreal 디버그 심볼은 ZIP에서 제외했다. Windows x64용 Visual C++ Redistributable과 Tk가 포함된 Python 3.11 이상이 필요하다. ZIP 크기는 410,148,145바이트이며 SHA-256은 `A6C09799E9B88DC6D68CCDB47DBD3E7501D8681E01DD4B9A30266F6892AF87F2`다.

모든 검증은 같은 Windows PC에서 수행했다. 3D 확인은 Unreal 센서 영상의 시점 변화로 검증했다. 별도 PC 시험과 실제 로컬 모델 시연은 남아 있다.
