# DeviceLink Studio Windows x64

1. ZIP 전체를 쓰기 가능한 폴더에 풉니다.
2. [Microsoft Visual C++ Redistributable x64](https://learn.microsoft.com/cpp/windows/latest-supported-vc-redist)와 Tk가 포함된 Python 3.11 이상을 설치합니다.
3. 압축을 푼 폴더에서 PowerShell로 실행합니다.

```powershell
python -m venv mcp_server/.venv
.\mcp_server\.venv\Scripts\python.exe -m pip install -r mcp_server/requirements-diagnostics.txt
.\DeviceLinkDemo.exe
```

Python Launcher를 사용한다면 첫 명령의 `python`을 `py -3`으로 바꿉니다. 가상 장비 실행 파일은 ZIP에 포함됩니다. 기본 데모는 LLM 없이 실행되며 로그와 보고서는 `artifacts/demo/`에 저장됩니다.

3D 화면을 보려면 런처에서 **3D 가상장비 화면 표시**를 선택한 뒤 **선택 모드로 준비**를 누릅니다. MFC의 **시험 / 설정**에서 Motor Stall을 주입하고 **현재 상태 진단**을 실행합니다. 고장을 해제한 뒤 **복구 검증**을 실행합니다.
