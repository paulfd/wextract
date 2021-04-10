project(WavetableExtract)
cmake_minimum_required(VERSION 3.11)
set (CMAKE_CXX_STANDARD 11)

find_package(OpenGL REQUIRED)
find_package(glfw3 QUIET)
if (NOT glfw3_FOUND)
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    add_subdirectory(third_party/glfw)
endif()

if (MSVC)
    add_library(sfizz SHARED IMPORTED)
    set_target_properties(sfizz PROPERTIES 
        IMPORTED_IMPLIB "${CMAKE_SOURCE_DIR}/third_party/sfizz/sfizz.lib"
        INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_SOURCE_DIR}/third_party/sfizz"
    )
else()
    find_package(PkgConfig REQUIRED) 
    pkg_check_modules(sfizz REQUIRED IMPORTED_TARGET GLOBAL sfizz)
    add_library(sfizz ALIAS PkgConfig::sfizz)
endif()

add_library(glad STATIC src/glad.c)
target_include_directories(glad PUBLIC include)

add_library(imgui STATIC 
    third_party/imgui/imgui.cpp
    third_party/imgui/imgui_demo.cpp
    third_party/imgui/imgui_draw.cpp
    third_party/imgui/imgui_tables.cpp
    third_party/imgui/imgui_widgets.cpp
    third_party/imgui/backends/imgui_impl_opengl3.cpp
    third_party/imgui/backends/imgui_impl_glfw.cpp
)
target_link_libraries(imgui PRIVATE glad glfw)
target_include_directories(imgui PUBLIC third_party/imgui)
target_include_directories(imgui PUBLIC third_party/imgui/backends)

add_library(implot STATIC 
    third_party/implot/implot.cpp
    third_party/implot/implot_items.cpp
)
target_link_libraries(implot PRIVATE imgui)
target_include_directories(implot PUBLIC third_party/implot)

add_library(miniaudio STATIC 
    third_party/miniaudio/extras/miniaudio_split/miniaudio.c
)
target_include_directories(miniaudio PUBLIC third_party/miniaudio/extras/miniaudio_split)
add_executable(wextract src/main.cpp)
target_include_directories(wextract PRIVATE include)        
target_link_libraries(wextract PRIVATE OpenGL::GL sfizz glad glfw imgui implot miniaudio)

if (UNIX)
    find_package(Threads)
    target_link_libraries(wextract PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
endif()

add_custom_command(TARGET wextract POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${PROJECT_SOURCE_DIR}/extras/sine_c3.wav"
        $<TARGET_FILE_DIR:wextract>)

if (MSVC)
    add_custom_command(TARGET wextract POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${PROJECT_SOURCE_DIR}/third_party/sfizz/$<CONFIG>/sfizz.dll"
            $<TARGET_FILE_DIR:wextract>)
endif()