pipeline {
    agent { label 'windows-unreal' }

    options {
        timestamps()
        disableConcurrentBuilds()
        buildDiscarder(logRotator(numToKeepStr: '20', artifactNumToKeepStr: '10'))
    }

    parameters {
        booleanParam(name: 'RUN_UNREAL_BUILD', defaultValue: true,
            description: 'Build the Unreal Editor module on the Windows Unreal agent.')
        booleanParam(name: 'RUN_UNREAL_SOAK', defaultValue: false,
            description: 'Run the optional unattended Unreal TCP integration soak test.')
        string(name: 'UNREAL_SOAK_CYCLES', defaultValue: '25',
            description: 'Protocol cycles for the optional Unreal soak test.')
    }

    environment {
        BUILD_DIRECTORY = 'build-jenkins'
        ARTIFACT_DIRECTORY = 'ci-artifacts'
    }

    stages {
        stage('Checkout') {
            steps {
                deleteDir()
                checkout scm
            }
        }

        stage('Validate Windows agent') {
            steps {
                powershell '''
                    $ErrorActionPreference = 'Stop'
                    foreach ($command in @('cmake', 'ctest')) {
                        if (-not (Get-Command $command -ErrorAction SilentlyContinue)) {
                            throw "Required command was not found on this agent: $command"
                        }
                    }
                    if (($env:RUN_UNREAL_BUILD -eq 'true' -or $env:RUN_UNREAL_SOAK -eq 'true') -and
                        [string]::IsNullOrWhiteSpace($env:UE_ROOT)) {
                        throw 'Set the UE_ROOT node environment variable to the Unreal Engine directory.'
                    }
                '''
            }
        }

        stage('Configure CMake') {
            steps {
                bat '''
                    @echo off
                    cmake -S . -B "%BUILD_DIRECTORY%" -G "Visual Studio 17 2022" -A x64 -DDEVICELINK_BUILD_MFC_HOST=ON
                '''
            }
        }

        stage('Build Windows host') {
            steps {
                bat '''
                    @echo off
                    cmake --build "%BUILD_DIRECTORY%" --config Release --parallel
                '''
            }
        }

        stage('Run C++ tests') {
            steps {
                bat '''
                    @echo off
                    if not exist "%ARTIFACT_DIRECTORY%" mkdir "%ARTIFACT_DIRECTORY%"
                    ctest --test-dir "%BUILD_DIRECTORY%" -C Release --output-on-failure --output-junit "%WORKSPACE%\%ARTIFACT_DIRECTORY%\ctest.xml"
                '''
            }
        }

        stage('Build Unreal virtual device') {
            when {
                expression { return params.RUN_UNREAL_BUILD || params.RUN_UNREAL_SOAK }
            }
            steps {
                powershell '''
                    $ErrorActionPreference = 'Stop'
                    $buildScript = Join-Path $env:UE_ROOT 'Engine\Build\BatchFiles\Build.bat'
                    $uproject = Join-Path $env:WORKSPACE 'UnrealVirtualDevice\UnrealVirtualDevice.uproject'
                    if (-not (Test-Path -LiteralPath $buildScript -PathType Leaf)) { throw "Build script not found: $buildScript" }
                    if (-not (Test-Path -LiteralPath $uproject -PathType Leaf)) { throw "Project not found: $uproject" }
                    & $buildScript UnrealVirtualDeviceEditor Win64 Development "-Project=$uproject" -WaitMutex
                    if ($LASTEXITCODE -ne 0) { throw "Unreal BuildTool failed with exit code $LASTEXITCODE." }
                '''
            }
        }

        stage('Run Unreal TCP soak') {
            when {
                expression { return params.RUN_UNREAL_SOAK }
            }
            steps {
                powershell '''
                    $ErrorActionPreference = 'Stop'
                    & "$env:WORKSPACE\ci\jenkins\Invoke-UnrealSoak.ps1" `
                        -UnrealRoot $env:UE_ROOT `
                        -ProjectRoot $env:WORKSPACE `
                        -BuildDirectory (Join-Path $env:WORKSPACE $env:BUILD_DIRECTORY) `
                        -Cycles ([int]$env:UNREAL_SOAK_CYCLES)
                    if ($LASTEXITCODE -ne 0) { throw "Unreal soak failed with exit code $LASTEXITCODE." }
                '''
            }
        }
    }

    post {
        always {
            junit allowEmptyResults: true, testResults: 'ci-artifacts/ctest.xml'
            archiveArtifacts allowEmptyArchive: true, artifacts: 'ci-artifacts/**/*, **/*.report.*.md'
        }
    }
}
