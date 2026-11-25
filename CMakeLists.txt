cmake_minimum_required(VERSION 3.20)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" STELLAR_VERSION)
string(STRIP "${STELLAR_VERSION}" STELLAR_VERSION)
project(StellarAnnals VERSION ${STELLAR_VERSION} LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Link the C++ runtime statically on Windows so the .exe runs without the VC++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(STELLAR_BUILD_VIEWER "Build the Dear ImGui viewer (downloads GLFW and Dear ImGui)" ON)

function(stellar_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
  endif()
endfunction()

# The simulation core: no graphics, no I/O beyond the event log.
add_library(annals_core STATIC
  src/names.cpp
  src/world.cpp
  src/event_log.cpp
  src/galaxy_gen.cpp
  src/sim.cpp
)
target_include_directories(annals_core PUBLIC src)
target_compile_definitions(annals_core PUBLIC STELLAR_VERSION="${STELLAR_VERSION}")
stellar_warnings(annals_core)

# Headless command-line runner.
add_executable(stellar_annals src/main.cpp)
target_link_libraries(stellar_annals PRIVATE annals_core)
stellar_warnings(stellar_annals)

if(STELLAR_BUILD_VIEWER)
  include(FetchContent)

  set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)  # X11 only on Linux; XWayland covers Wayland desktops

  FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.4
    GIT_SHALLOW TRUE)
  FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.91.5
    GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(glfw imgui)

  find_package(OpenGL REQUIRED)

  add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
  )
  target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
  target_link_libraries(imgui PUBLIC glfw OpenGL::GL)

  add_executable(stellar_annals_viewer viewer/main.cpp viewer/viewer.cpp)
  target_link_libraries(stellar_annals_viewer PRIVATE annals_core imgui)
  stellar_warnings(stellar_annals_viewer)
endif()
