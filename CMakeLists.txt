project(WavetableExtract)
cmake_minimum_required(VERSION 3.14)
set (CMAKE_CXX_STANDARD 17)
set (CMAKE_EXPORT_COMPILE_COMMANDS ON)
include(cmake/CPM.cmake)

find_package(OpenGL REQUIRED)

CPMFindPackage(
    NAME glfw3
    GITHUB_REPOSITORY "glfw/glfw"
    GIT_TAG 3.3.8
)
CPMFindPackage(
    NAME fmt
    GITHUB_REPOSITORY "fmtlib/fmt"
    GIT_TAG 9.1.0
)
CPMFindPackage(
    NAME sfizz
    GITHUB_REPOSITORY "sfztools/sfizz"
    GIT_TAG 1.2.0
)

# Static dependencies
add_library(glad STATIC src/glad.c)
target_include_directories(glad PUBLIC include)

CPMAddPackage("gl:libeigen/eigen#1148f0a9ec48bcedac69c9ed66d4b2f6bab89344")

CPMAddPackage("gh:ocornut/imgui@1.86")
add_library(imgui STATIC 
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl2.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
)
target_link_libraries(imgui PRIVATE glad glfw)
target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR})
target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR}/backends)

CPMAddPackage("gh:epezent/implot@0.12")
add_library(implot STATIC ${implot_SOURCE_DIR}/implot.cpp ${implot_SOURCE_DIR}/implot_items.cpp)
target_link_libraries(implot PRIVATE imgui)
target_include_directories(implot PUBLIC ${implot_SOURCE_DIR})

CPMAddPackage("gh:mackron/miniaudio#fca829edefd8389380f8e3ee26cc4b8c426dd742")
add_library(miniaudio STATIC ${miniaudio_SOURCE_DIR}/extras/miniaudio_split/miniaudio.c)
target_include_directories(miniaudio PUBLIC  ${miniaudio_SOURCE_DIR}/extras/miniaudio_split)

CPMAddPackage(
    NAME kissfft
    GITHUB_REPOSITORY "mborgerding/kissfft"
    GIT_TAG "131.1.0" 
    OPTIONS
    "KISSFFT_STATIC ON"
    "KISSFFT_TEST OFF"
    "KISSFFT_TOOLS OFF"
    "KISSFFT_PKGCONFIG OFF"
)

# Make a non-console window
if(MSVC)
	set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup")
endif()

add_executable(wextract src/main.cpp src/helpers.cpp src/synth.cpp)
target_include_directories(wextract PRIVATE include third_party/imgui-filebrowser)        
target_link_libraries(wextract PRIVATE OpenGL::GL sfizz_shared glad glfw imgui 
    implot miniaudio fmt::fmt Eigen3::Eigen kissfft::kissfft)
install(TARGETS wextract DESTINATION ${CMAKE_INSTALL_BINDIR})

# Disable note:
# > note: the ABI of passing structure with ‘complex float’ member has changed in GCC 4.4
if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(wextract PRIVATE "-Wno-psabi")
endif()

if (UNIX)
    find_package(Threads)
    target_link_libraries(wextract PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
endif()

add_custom_command(TARGET wextract POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${PROJECT_SOURCE_DIR}/extras/montre8_c2.wav"
        $<TARGET_FILE_DIR:wextract>)

add_custom_command(TARGET wextract POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${PROJECT_SOURCE_DIR}/extras/trompette8_c2.wav"
            $<TARGET_FILE_DIR:wextract>)

if (MSVC)
    add_custom_command(TARGET wextract POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${PROJECT_SOURCE_DIR}/third_party/sfizz/$<CONFIG>/sfizz.dll"
            $<TARGET_FILE_DIR:wextract>)
endif()

if(NOT MSVC)
    install(TARGETS wextract RUNTIME)
endif()
