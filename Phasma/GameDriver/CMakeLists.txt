cmake_minimum_required(VERSION 3.22)
project(PhasmaGameDriver LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "PhasmaGameDriver currently uses Windows WinHTTP")
endif()

add_library(PhasmaGameProtocol STATIC Protocol.cpp)
target_compile_features(PhasmaGameProtocol PUBLIC cxx_std_20)
target_include_directories(PhasmaGameProtocol PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_include_directories(PhasmaGameProtocol SYSTEM PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../MCP/third_party")

add_executable(PhasmaGameDriver
    main.cpp
    Http.cpp
    Vault.cpp)
target_link_libraries(PhasmaGameDriver PRIVATE
    PhasmaGameProtocol
    winhttp
    windowsapp)
target_compile_definitions(PhasmaGameDriver PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

if(MSVC)
    target_compile_options(PhasmaGameProtocol PRIVATE /W4 /permissive- /utf-8)
    target_compile_options(PhasmaGameDriver PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(PhasmaGameProtocol PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(PhasmaGameDriver PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(PhasmaGameDriverTests tests/ProtocolTests.cpp)
    target_link_libraries(PhasmaGameDriverTests PRIVATE PhasmaGameProtocol)
    add_test(NAME game-driver-protocol COMMAND PhasmaGameDriverTests)

    find_program(PHASMA_POWERSHELL NAMES powershell.exe pwsh.exe REQUIRED)
    add_test(NAME game-driver-launcher
        COMMAND "${PHASMA_POWERSHELL}" -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_CURRENT_SOURCE_DIR}/tests/LauncherTests.ps1")
    set_tests_properties(game-driver-launcher PROPERTIES TIMEOUT 15)

    add_test(NAME game-driver-realtime
        COMMAND "${PHASMA_POWERSHELL}" -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_CURRENT_SOURCE_DIR}/tests/RealtimeLoopTests.ps1")
    set_tests_properties(game-driver-realtime PROPERTIES TIMEOUT 60)

    find_package(Python3 QUIET COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        add_test(NAME game-driver-mock-mcp
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/mock_mcp.py"
            "$<TARGET_FILE:PhasmaGameDriver>")
        set_tests_properties(game-driver-mock-mcp PROPERTIES TIMEOUT 45)
    endif()
endif()
