cmake_minimum_required(VERSION 3.10)

project(CMakeTarget)

set(CMAKE_CXX_STANDARD 17)

add_executable(CMakeTarget
    Monitor_test.cpp
    src/collectors/cpu/cpu.cpp
    src/collectors/ram/ram.cpp
    src/collectors/systeminfo/systemInfo.cpp
)

# ---- DEFINES (ВАЖНО: после add_executable)
target_compile_definitions(CMakeTarget PRIVATE
    _WIN32_WINNT=0x0601
    _CRT_SECURE_NO_WARNINGS
)

# ---- WINDOWS LIBS
if (WIN32)
    target_link_libraries(CMakeTarget
        pdh
        PowrProf
    )
endif()