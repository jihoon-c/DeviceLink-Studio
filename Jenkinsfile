pipeline {
    agent { label 'windows-unreal' }

    options {
        skipDefaultCheckout()
        timestamps()
        disableConcurrentBuilds()
        skipStagesAfterUnstable()
        timeout(time: 60, unit: 'MINUTES')
        buildDiscarder(logRotator(numToKeepStr: '20', artifactNumToKeepStr: '5'))
    }

    parameters {
        string(name: 'RELEASE_TAG', defaultValue: '', trim: true,
            description: 'Optional existing remote vX.Y.Z tag that points at the checked-out revision.')
        booleanParam(name: 'CREATE_GITHUB_DRAFT', defaultValue: false,
            description: 'Upload a verified tagged build to a GitHub Release draft.')
    }

    environment {
        GITHUB_REPOSITORY = 'jihoon-c/DeviceLink-Studio'
    }

    stages {
        stage('Checkout') {
            steps {
                deleteDir()
                checkout scm
                pwsh '''
                    $ErrorActionPreference = 'Stop'
                    if ($env:CREATE_GITHUB_DRAFT -eq 'true' -and [string]::IsNullOrWhiteSpace($env:RELEASE_TAG)) {
                        throw 'Set RELEASE_TAG to an existing version tag before requesting a draft.'
                    }
                    if ([string]::IsNullOrWhiteSpace($env:UE_ROOT)) {
                        throw 'Set UE_ROOT on the Windows Unreal agent.'
                    }
                '''
            }
        }

        stage('Windows build and regression') {
            steps {
                pwsh './ci/Invoke-WindowsCI.ps1'
            }
            post {
                always {
                    junit allowEmptyResults: true, testResults: 'ci-artifacts/ctest.xml'
                }
            }
        }

        stage('Package and verify release ZIP') {
            steps {
                pwsh './ci/jenkins/Build-WindowsRelease.ps1 -EngineRoot $env:UE_ROOT'
            }
        }

        stage('Create GitHub release draft') {
            when { expression { params.CREATE_GITHUB_DRAFT } }
            steps {
                withCredentials([string(credentialsId: 'devicelink-github-release', variable: 'GH_TOKEN')]) {
                    pwsh './ci/jenkins/New-GitHubReleaseDraft.ps1 -Tag $env:RELEASE_TAG -Repository $env:GITHUB_REPOSITORY'
                }
            }
        }
    }

    post {
        always {
            archiveArtifacts allowEmptyArchive: true, fingerprint: true,
                artifacts: 'ci-artifacts/**/*, build-demo/build-demo.log'
        }
    }
}
