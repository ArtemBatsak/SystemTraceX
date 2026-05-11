cmake_minimum_required(VERSION 3.10)

project(SystemTraceX)

set(CMAKE_CXX_STANDARD 17)

add_executable(SystemTraceX
    Monitor_test.cpp
    src/collectors/cpu/cpu.cpp
    src/collectors/ram/ram.cpp
    src/collectors/systeminfo/systemInfo.cpp
    src/collectors/disk/disk.cpp
    #src/collectors/process/process.cpp
   # src/collectors/errors/errors.cpp
    #src/collectors/network/network.cpp
    "src/collectors/cpu/cpu.h")
 #"src/collectors/network/network.h" "src/collectors/network/network.cpp" "src/collectors/disk/disk.h" "src/collectors/disk/disk.cpp" "src/collectors/process/process.h" "src/collectors/process/process.cpp" "src/collectors/errors/errors.cpp" "src/collectors/errors/errors.h")

# ---- DEFINES (ВАЖНО: после add_executable)
target_compile_definitions(SystemTraceX PRIVATE
    _WIN32_WINNT=0x0601
    _CRT_SECURE_NO_WARNINGS
)

# ---- WINDOWS LIBS
if (WIN32)
    target_link_libraries(SystemTraceX
        pdh
        PowrProf
    )
endif()

