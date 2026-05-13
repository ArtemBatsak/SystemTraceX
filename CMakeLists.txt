cmake_minimum_required(VERSION 3.10)
project(SystemTraceX)

set(CMAKE_CXX_STANDARD 17)

add_executable(SystemTraceX
    Monitor_test.cpp
    src/collectors/cpu/cpu.cpp
    src/collectors/ram/ram.cpp
    src/collectors/systeminfo/systemInfo.cpp
    src/collectors/disk/disk.cpp
    src/collectors/errors/errors.cpp
    src/collectors/network/network.cpp
    src/collectors/process/process.cpp
)

# ---- DEFINES
target_compile_definitions(SystemTraceX PRIVATE
    _WIN32_WINNT=0x0601
    _CRT_SECURE_NO_WARNINGS
)

# ---- LIBS (Разделяем по платформам)

if (WIN32)
    target_link_libraries(SystemTraceX PRIVATE
        pdh
        PowrProf
        wevtapi
        iphlpapi    
        ws2_32
    )
endif()

