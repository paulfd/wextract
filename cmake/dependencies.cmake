find_package(OpenGL REQUIRED)
include(cmake/CPM.cmake)

CPMFindPackage(
    NAME glfw3
    GITHUB_REPOSITORY "glfw/glfw"
    GIT_TAG 3.3.2
    DOWNLOAD_ONLY ${WEXTRACT_DOWNLOAD_DEPENDENCIES_ONLY}
)
CPMFindPackage(
    NAME fmt
    GITHUB_REPOSITORY "fmtlib/fmt"
    GIT_TAG 6.1.0
    DOWNLOAD_ONLY ${WEXTRACT_DOWNLOAD_DEPENDENCIES_ONLY}
)
CPMFindPackage(
    NAME sfizz
    GITHUB_REPOSITORY "sfztools/sfizz"
    GIT_TAG 1.2.0
    DOWNLOAD_ONLY ${WEXTRACT_DOWNLOAD_DEPENDENCIES_ONLY}
)

# Static dependencies
add_library(glad STATIC src/glad.c)
target_include_directories(glad PUBLIC include)

CPMAddPackage(
    NAME eigen
    GITLAB_REPOSITORY "libeigen/eigen"
    GIT_TAG "1148f0a9ec48bcedac69c9ed66d4b2f6bab89344"
    DOWNLOAD_ONLY ${WEXTRACT_DOWNLOAD_DEPENDENCIES_ONLY}
)

CPMAddPackage(
    NAME imgui
    GITHUB_REPOSITORY "ocornut/imgui"
    GIT_TAG "v1.86"
    DOWNLOAD_ONLY ${WEXTRACT_DOWNLOAD_DEPENDENCIES_ONLY}
)

add_library(imgui STATIC 
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
)
target_link_libraries(imgui PRIVATE glad glfw)
target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR})
target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR}/backends)

CPMAddPackage(
    NAME implot
    GITHUB_REPOSITORY "epezent/implot"
    GIT_TAG "v0.12"
    DOWNLOAD_ONLY ${WEXTRACT_DOWNLOAD_DEPENDENCIES_ONLY}
)
add_library(implot STATIC ${implot_SOURCE_DIR}/implot.cpp ${implot_SOURCE_DIR}/implot_items.cpp)
target_link_libraries(implot PRIVATE imgui)
target_include_directories(implot PUBLIC ${implot_SOURCE_DIR})

CPMAddPackage(
    NAME miniaudio
    GITHUB_REPOSITORY "mackron/miniaudio"
    GIT_TAG "fca829edefd8389380f8e3ee26cc4b8c426dd742"
    DOWNLOAD_ONLY ${WEXTRACT_DOWNLOAD_DEPENDENCIES_ONLY}
)
add_library(miniaudio STATIC ${miniaudio_SOURCE_DIR}/extras/miniaudio_split/miniaudio.c)
target_include_directories(miniaudio PUBLIC  ${miniaudio_SOURCE_DIR}/extras/miniaudio_split)

CPMAddPackage(
    NAME kissfft
    GITHUB_REPOSITORY "mborgerding/kissfft"
    GIT_TAG "131.1.0" 
    DOWNLOAD_ONLY ${WEXTRACT_DOWNLOAD_DEPENDENCIES_ONLY}
    OPTIONS
        "KISSFFT_STATIC ON"
        "KISSFFT_TEST OFF"
        "KISSFFT_TOOLS OFF"
        "KISSFFT_PKGCONFIG OFF"
)
CPMAddPackage(
    NAME imgui_filebrowser
    GITHUB_REPOSITORY "AirGuanZ/imgui-filebrowser"
    GIT_TAG "cfccc2a"
    DOWNLOAD_ONLY ${WEXTRACT_DOWNLOAD_DEPENDENCIES_ONLY}
)
add_library(imgui_filebrowser INTERFACE)
target_include_directories(imgui_filebrowser INTERFACE ${imgui_filebrowser_SOURCE_DIR})
