cmake_minimum_required(VERSION 3.20)

project(DeviceLinkStudio LANGUAGES C CXX)

if(NOT WIN32)
    message(FATAL_ERROR "DeviceLink Studio currently targets Windows only.")
endif()

add_library(SQLite3 STATIC
    third_party/sqlite-amalgamation-3530400/sqlite3.c
)

target_include_directories(SQLite3 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/sqlite-amalgamation-3530400
)

add_library(DeviceLinkInfrastructure STATIC
    src/Infrastructure/AtomicTextFileStore.cpp
    src/Infrastructure/AsyncLogger.cpp
    src/Infrastructure/AsyncEventStore.cpp
    src/Infrastructure/SqliteEventRepository.cpp
    src/Infrastructure/TextFileLogger.cpp
)

target_include_directories(DeviceLinkInfrastructure
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(DeviceLinkInfrastructure PUBLIC cxx_std_20)
target_link_libraries(DeviceLinkInfrastructure PRIVATE SQLite3)

if(MSVC)
    target_compile_options(DeviceLinkInfrastructure PRIVATE /W4 /permissive- /EHsc)
endif()

add_library(DeviceLinkTransport STATIC
    src/Transport/TcpTransport.cpp
)

target_include_directories(DeviceLinkTransport
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(DeviceLinkTransport PUBLIC cxx_std_20)
target_compile_definitions(DeviceLinkTransport PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(DeviceLinkTransport PRIVATE ws2_32)

if(MSVC)
    target_compile_options(DeviceLinkTransport PRIVATE /W4 /permissive- /EHsc)
endif()

add_library(DeviceLinkProtocol STATIC
    src/Protocol/Crc16.cpp
    src/Protocol/PacketFrame.cpp
    src/Protocol/PacketHeader.cpp
)

target_include_directories(DeviceLinkProtocol
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(DeviceLinkProtocol PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(DeviceLinkProtocol PRIVATE /W4 /permissive- /EHsc)
endif()

add_library(DeviceLinkCore STATIC
    src/Core/ConnectionStateMachine.cpp
    src/Core/DeviceSession.cpp
)

target_include_directories(DeviceLinkCore
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(DeviceLinkCore PUBLIC cxx_std_20)
target_link_libraries(DeviceLinkCore PRIVATE DeviceLinkProtocol DeviceLinkTransport)

if(MSVC)
    target_compile_options(DeviceLinkCore PRIVATE /W4 /permissive- /EHsc)
endif()

add_library(DeviceLinkApplication STATIC
    src/Application/AxisVapixCommandAdapter.cpp
    src/Application/CommunicationAlarmService.cpp
    src/Application/DeviceEventProcessingService.cpp
    src/Application/DeviceEventPersistence.cpp
    src/Application/DeviceEventQueue.cpp
    src/Application/DeviceManager.cpp
    src/Application/DeviceRuntime.cpp
    src/Application/EventLogQueryService.cpp
    src/Application/ScenarioFileService.cpp
    src/Application/ScenarioProgressQueue.cpp
    src/Application/ScenarioReportService.cpp
    src/Application/FrameReplayRunner.cpp
    src/Application/GimbalProtocolAdapter.cpp
    src/Application/OperatorWorkflowService.cpp
    src/Application/OperatorSettingsService.cpp
    src/Application/ScenarioRunner.cpp
    src/Application/ScenarioTextCodec.cpp
    src/Application/SqliteFrameReplaySource.cpp
    src/Application/TelemetryService.cpp
    src/Application/TelemetryQualityService.cpp
    src/Application/TelemetryHeartbeatWatchdog.cpp
    src/Application/VirtualGimbalService.cpp
)

target_include_directories(DeviceLinkApplication
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(DeviceLinkApplication PUBLIC cxx_std_20)
target_link_libraries(DeviceLinkApplication PRIVATE DeviceLinkCore DeviceLinkInfrastructure)

if(MSVC)
    target_compile_options(DeviceLinkApplication PRIVATE /W4 /permissive- /EHsc)
endif()

add_library(DeviceLinkUI STATIC
    src/UI/DashboardBinding.cpp
    src/UI/DashboardModel.cpp
    src/UI/WindowEventBridge.cpp
)

target_include_directories(DeviceLinkUI
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(DeviceLinkUI PUBLIC cxx_std_20)
target_compile_definitions(DeviceLinkUI PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(DeviceLinkUI PRIVATE DeviceLinkApplication user32)

if(MSVC)
    target_compile_options(DeviceLinkUI PRIVATE /W4 /permissive- /EHsc)
endif()

option(DEVICELINK_BUILD_MFC_INTEGRATION "Build the MFC dashboard integration library" OFF)
option(DEVICELINK_BUILD_MFC_HOST "Build the standalone MFC dashboard host example" OFF)

if(DEVICELINK_BUILD_MFC_HOST)
    set(DEVICELINK_BUILD_MFC_INTEGRATION ON)
endif()

if(DEVICELINK_BUILD_MFC_INTEGRATION)
    set(CMAKE_MFC_FLAG 2)

    add_library(DeviceLinkMfcIntegration STATIC
        src/UI/MfcDashboardFrame.cpp
    )
    target_include_directories(DeviceLinkMfcIntegration
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_compile_features(DeviceLinkMfcIntegration PUBLIC cxx_std_20)
    target_compile_definitions(DeviceLinkMfcIntegration PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _AFXDLL UNICODE _UNICODE)
    target_link_libraries(DeviceLinkMfcIntegration PRIVATE DeviceLinkUI DeviceLinkApplication user32)

    if(MSVC)
        target_compile_options(DeviceLinkMfcIntegration PRIVATE /W4 /permissive- /EHsc /utf-8)
    endif()
endif()

if(DEVICELINK_BUILD_MFC_HOST)
    add_executable(DeviceLinkMfcHost WIN32
        src/MfcHost/MfcHostMain.cpp
    )
    target_compile_features(DeviceLinkMfcHost PRIVATE cxx_std_20)
    target_compile_definitions(DeviceLinkMfcHost PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _AFXDLL UNICODE _UNICODE)
    target_link_libraries(DeviceLinkMfcHost PRIVATE DeviceLinkMfcIntegration DeviceLinkApplication DeviceLinkInfrastructure DeviceLinkTransport user32)

    if(MSVC)
        target_compile_options(DeviceLinkMfcHost PRIVATE /W4 /permissive- /EHsc)
    endif()
endif()

add_executable(DeviceLinkSimulator
    src/Simulator/ConsoleSimulator.cpp
)

target_compile_features(DeviceLinkSimulator PRIVATE cxx_std_20)
target_compile_definitions(DeviceLinkSimulator PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(DeviceLinkSimulator PRIVATE DeviceLinkProtocol ws2_32)

if(MSVC)
    target_compile_options(DeviceLinkSimulator PRIVATE /W4 /permissive- /EHsc)
endif()

add_executable(DeviceLinkUnrealSoak
    src/Tools/UnrealIntegrationSoak.cpp
)
target_compile_features(DeviceLinkUnrealSoak PRIVATE cxx_std_20)
target_compile_definitions(DeviceLinkUnrealSoak PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(DeviceLinkUnrealSoak PRIVATE DeviceLinkApplication DeviceLinkTransport ws2_32)

if(MSVC)
    target_compile_options(DeviceLinkUnrealSoak PRIVATE /W4 /permissive- /EHsc)
endif()

include(CTest)

if(BUILD_TESTING)
    add_executable(TcpTransportTests
        tests/Transport/TcpTransportTests.cpp
    )
    target_compile_features(TcpTransportTests PRIVATE cxx_std_20)
    target_compile_definitions(TcpTransportTests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(TcpTransportTests PRIVATE DeviceLinkTransport ws2_32)

    if(MSVC)
        target_compile_options(TcpTransportTests PRIVATE /W4 /permissive- /EHsc)
    endif()

    add_test(NAME TcpTransportTests COMMAND TcpTransportTests)

    add_executable(ProtocolTests
        tests/Protocol/ProtocolTests.cpp
    )
    target_compile_features(ProtocolTests PRIVATE cxx_std_20)
    target_link_libraries(ProtocolTests PRIVATE DeviceLinkProtocol)

    if(MSVC)
        target_compile_options(ProtocolTests PRIVATE /W4 /permissive- /EHsc)
    endif()

    add_test(NAME ProtocolTests COMMAND ProtocolTests)

    add_executable(ApplicationTests
        tests/Application/ApplicationTests.cpp
        tests/Core/DeviceSessionTests.cpp
    )
    target_compile_features(ApplicationTests PRIVATE cxx_std_20)
    target_compile_definitions(ApplicationTests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(ApplicationTests PRIVATE DeviceLinkApplication DeviceLinkInfrastructure DeviceLinkTransport DeviceLinkUI ws2_32)

    if(MSVC)
        target_compile_options(ApplicationTests PRIVATE /W4 /permissive- /EHsc)
    endif()

    add_test(NAME ApplicationTests COMMAND ApplicationTests)

    add_executable(InfrastructureTests
        tests/Infrastructure/InfrastructureTests.cpp
    )
    target_compile_features(InfrastructureTests PRIVATE cxx_std_20)
    target_link_libraries(InfrastructureTests PRIVATE DeviceLinkInfrastructure)

    if(MSVC)
        target_compile_options(InfrastructureTests PRIVATE /W4 /permissive- /EHsc)
    endif()

    add_test(NAME InfrastructureTests COMMAND InfrastructureTests)
endif()
