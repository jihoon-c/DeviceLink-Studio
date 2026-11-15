# Jenkins CI/CD 운영

Jenkins 컨트롤러는 Docker Compose에 두고, 실제 작업은 Unreal Engine 5.8과 MFC가 설치된 Windows 에이전트에서 실행한다. GitHub Actions의 PR 작업은 호스팅 러너에서 실행한다.

## 컨트롤러와 에이전트

```powershell
docker compose -f infra/jenkins/docker-compose.yml up -d
```

1. 같은 PC에서 `http://localhost:8080`을 열고 초기 설정을 완료한다. Pipeline, Git, JUnit, Credentials Binding, Timestamper 플러그인을 설치한다. 초기 암호는 로컬 콘솔에서 `docker compose -f infra/jenkins/docker-compose.yml exec jenkins cat /var/jenkins_home/secrets/initialAdminPassword`로 확인한다.
2. **Manage Jenkins → Nodes → New Node**에서 고정 에이전트를 추가한다. label은 `windows-unreal`, executor는 1개, 작업 디렉터리는 공백이 없는 짧은 경로로 지정한다. **Launch agent by connecting it to the controller**를 선택한다.
3. Windows 에이전트에 Java 21, PowerShell 7, Git, Python 3.12(Tk 포함), Visual Studio C++/MFC·Windows SDK·CMake/Ninja, Unreal Engine 5.8을 준비한다. `pwsh`, `python`, `git`을 에이전트 계정의 PATH에서 확인하고, 노드 환경 변수 `UE_ROOT`를 실제 설치 위치로 지정한다.
4. 노드 페이지에 표시되는 명령으로 에이전트를 연결한다. 같은 Windows PC라면 URL은 `http://127.0.0.1:8080/`이며 `-webSocket` 옵션을 사용한다. 에이전트 계정에서 Unreal 패키징을 먼저 실행해 설치·캐시 접근을 확인한다.
5. **New Item → Pipeline → Pipeline script from SCM**으로 작업을 생성한다. Git URL과 신뢰된 브랜치(예: `*/develop`), Script Path `Jenkinsfile`을 지정한다. 첫 실행은 기본값으로 시작한다. 작업 전 TCP 5000 사용 여부를 확인한다. 같은 엔진을 쓰는 다른 Unreal 패키징 작업은 종료한 뒤 실행한다.

컨트롤러 상태와 실행 기록은 Docker의 `jenkins_home` 볼륨에 저장된다. 다른 장비에 에이전트를 둔다면 HTTPS를 통해 접근할 수 있도록 컨트롤러 연결 주소를 구성한다.

## 릴리스 후보와 GitHub 초안

기본 실행은 Windows 회귀 시험, Unreal 패키징, ZIP 재추출, 루프백 고장 복구 스모크를 실행한다. 최종 ZIP과 체크섬은 검증을 통과한 경우 빌드의 **Artifacts**에 나타난다.

- `build-demo/build-demo.log`, `ci-artifacts/ctest.xml`
- `ci-artifacts/unreal-package.log`, `ci-artifacts/smoke/`
- `ci-artifacts/release/DeviceLinkStudio-Windows-x64.zip` 및 `.sha256`

초안을 생성하려면 Windows 에이전트에 GitHub CLI(`gh`)를 준비하고, 대상 저장소의 Contents 쓰기 권한을 가진 토큰을 Jenkins에 **Secret text** `devicelink-github-release`로 등록한다. 원격 태그가 작업에서 체크아웃한 커밋을 가리키는지 확인한 뒤 `RELEASE_TAG`에 태그를 입력하고 `CREATE_GITHUB_DRAFT`를 선택한다. Jenkinsfile의 `GITHUB_REPOSITORY`가 발행 대상이다. 초안과 첨부 파일을 확인한 후 공개한다.

## 로컬 재현

신규 작업 폴더에서 PowerShell 7로 실행한다. 앞서 만든 Unreal 패키지나 릴리스 폴더가 있으면 새 작업 폴더를 사용한다.

```powershell
.\ci\Invoke-WindowsCI.ps1
.\ci\jenkins\Build-WindowsRelease.ps1 -EngineRoot 'F:\UE_5.8'
Get-FileHash .\ci-artifacts\release\DeviceLinkStudio-Windows-x64.zip -Algorithm SHA256
Get-Content .\ci-artifacts\release\DeviceLinkStudio-Windows-x64.zip.sha256
```

[파이프라인 설계와 시연 순서](CICD_PORTFOLIO_KR.md)
