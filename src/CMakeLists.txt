# Make a non-console window
if(MSVC)
	set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup")
endif()

add_executable(wextract main.cpp helpers.cpp synth.cpp)
target_link_libraries(wextract PRIVATE OpenGL::GL sfizz_shared glad glfw imgui 
    implot miniaudio fmt::fmt Eigen3::Eigen kissfft::kissfft imgui_filebrowser)
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

if(NOT MSVC)
    install(TARGETS wextract RUNTIME)
else()
    add_custom_command(TARGET wextract POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy -t $<TARGET_FILE_DIR:wextract> $<TARGET_RUNTIME_DLLS:wextract>
    COMMAND_EXPAND_LISTS
    )
endif()