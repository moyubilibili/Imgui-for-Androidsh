cmake_minimum_required(VERSION 3.16)
project(ImGuiNativeBinary)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 可执行程序名称
set(TARGET ImGuiBinary)

# ============================================================
# ImGui 源码（本地路径）
# ============================================================
set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/imgui)

set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)

# ============================================================
# 可执行程序
# ============================================================
add_executable(${TARGET}
    src/main.cpp
    ${IMGUI_SOURCES}
)

target_include_directories(${TARGET} PRIVATE
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)

# Android NDK 链接
target_link_libraries(${TARGET} PRIVATE
    EGL
    GLESv3
    android
    log
)

# 静态链接 C++ 运行时，减少设备依赖
set_target_properties(${TARGET} PROPERTIES
    LINK_FLAGS "-static-libstdc++"
)

message(STATUS "Building native executable: ${TARGET}")
message(STATUS "ImGui source: ${IMGUI_DIR}")