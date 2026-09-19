# Jenkins CI 설정 가이드

## 목적

DeviceLink Studio의 Jenkins 구성은 **Docker Controller + Windows Unreal Agent** 구조를 사용한다.
Controller는 파이프라인, 인증, 실행 이력과 artifact만 관리하고, Windows Agent가 MFC, Winsock2,
Visual Studio, CMake, Unreal Engine이 필요한 실제 빌드와 시험을 실행한다. Unreal Editor와 MFC를
Linux 컨테이너에서 실행하지 않는 이유는 Windows GUI, MFC 런타임, Unreal Engine 및 GPU 의존성을
Windows 환경에서 그대로 검증하기 위해서다.

## 저장소 구성

- `Jenkinsfile`: Pipeline-as-Code 정의. `develop`과 PR에 사용할 Multibranch Pipeline의 진입점이다.
- `infra/jenkins/docker-compose.yml`: Jenkins Controller 실행 구성이다.
- `ci/jenkins/Invoke-UnrealSoak.ps1`: 선택 실행하는 Unreal TCP 통합 Soak 스크립트다.

## 1. Jenkins Controller 시작

Docker Desktop 또는 Docker Engine이 설치된 PC에서 아래를 실행한다.

```powershell
cd infra/jenkins
docker compose up -d
docker compose exec jenkins cat /var/jenkins_home/secrets/initialAdminPassword
```

브라우저에서 `http://localhost:8080`을 열고 표시된 초기 비밀번호로 접속한다. Suggested Plugins를
설치한 뒤 Git, Pipeline, JUnit 플러그인이 활성화됐는지 확인한다. Controller의 `8080`은 웹 관리 화면,
`50000`은 inbound agent 연결 포트다.

## 2. Windows Unreal Agent 준비

Windows Agent에는 아래 항목이 필요하다.

1. Visual Studio 2022 C++ Desktop Development, MFC, Windows SDK
2. CMake와 Git (PATH 등록)
3. Unreal Engine 5.8과 이 프로젝트의 빌드 권한
4. Jenkins agent Java 런타임
5. Jenkins Node label: `windows-unreal`
6. Node Environment Variable: `UE_ROOT=F:\UE_5.8` (설치 위치에 맞게 변경)

Jenkins 관리 화면에서 **Manage Jenkins → Nodes → New Node**로 agent를 생성하고 label에
`windows-unreal`을 지정한다. Controller가 Docker에 있어도 Agent는 Windows PC에서 서비스 또는
inbound agent로 실행한다.

## 3. GitHub 연동

1. Jenkins에서 **New Item → Multibranch Pipeline**을 선택한다.
2. GitHub 저장소 `jihoon-c/DeviceLink-Studio`를 branch source로 등록한다.
3. Jenkins Credentials에 GitHub Fine-grained PAT를 등록한다.
4. PAT는 해당 저장소에 대해 최소 `Contents: Read` 권한을 가져야 한다. private repository라면
   repository access도 명시적으로 허용한다.
5. Scan Multibranch Pipeline Now를 실행한다.

`Jenkinsfile`은 `windows-unreal` label이 있는 agent에서만 실행된다. 기본 파이프라인은 CMake
Release 구성, MFC Host 빌드, CTest를 실행하고 JUnit XML과 Markdown 보고서를 artifact로 보관한다.

## 4. Unreal 검증 선택 실행

- `RUN_UNREAL_BUILD=true`: UnrealVirtualDeviceEditor C++ 모듈을 빌드한다.
- `RUN_UNREAL_SOAK=true`: Unreal 게임을 무인 실행하고 `DeviceLinkUnrealSoak`으로 TCP 명령,
  ACK, telemetry, 재연결을 검증한다.
- `UNREAL_SOAK_CYCLES`: 기본 25회이며 CI 초기 도입 단계에서는 25~100회를 권장한다.

Soak 테스트는 Unreal 프로세스를 직접 시작하고 종료하므로, 일반 개발자 PC와 분리된 Windows Agent에서
실행하는 것이 좋다. Agent에서 포트 `5510`을 다른 프로세스가 사용하지 않아야 한다.

## 운영 원칙

- `Jenkinsfile`을 수정하면 Pipeline 변경도 코드 리뷰와 Git 이력에 남긴다.
- Controller 볼륨 `jenkins_home`은 삭제하지 말고 Docker 백업 정책에 포함한다.
- GitHub PAT, Unreal 라이선스 정보, 비밀번호는 저장소나 Jenkinsfile에 기록하지 않는다.
- Windows Agent의 Unreal 버전과 `UE_ROOT`는 프로젝트의 `.uproject` Engine Association과 맞춘다.
