cmake_minimum_required(VERSION 3.10)
project(SystemTraceX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Определяем тип исполняемого файла в зависимости от ОС
if(WIN32)
    set(EXE_TYPE WIN32)
else()
    set(EXE_TYPE "")
endif()

# ---------------- EXECUTABLE ----------------
# Передаем ${EXE_TYPE} — на Windows это уберет консоль, на Linux проигнорируется
add_executable(SystemTraceX ${EXE_TYPE}
    SystemTraceX.cpp

    src/collectors/cpu/cpu.cpp
    src/collectors/ram/ram.cpp
    src/collectors/systeminfo/systemInfo.cpp
    src/collectors/disk/disk.cpp
    src/collectors/errors/errors.cpp
    src/collectors/network/network.cpp
    src/collectors/process/process.cpp
    src/collector/collector.cpp
    src/collectors/gpu/gpu.cpp

    src/web/web_helper.cpp
    src/web/web.cpp
)

# ---------------- VCPKG PACKAGES ----------------
find_package(httplib CONFIG REQUIRED)
target_link_libraries(SystemTraceX PRIVATE
    httplib::httplib
)

# ---------------- WINDOWS LIBS ----------------
if (WIN32)
    target_compile_definitions(SystemTraceX PRIVATE
        _WIN32_WINNT=0x0A00
        _CRT_SECURE_NO_WARNINGS
    )
    target_link_libraries(SystemTraceX PRIVATE
        pdh
        PowrProf
        wevtapi
        iphlpapi
        ws2_32
        shell32 # Обязательно добавляем для Shell_NotifyIcon (трея)
        user32  # Обязательно добавляем для работы с окнами/сообщениями
    )
endif()