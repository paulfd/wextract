#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <string>
#include <array>
#include <algorithm>
#include <iostream>
#include "imgui.h"
#include "implot.h"
#include "implot_internal.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "sfizz.hpp"
#include "defer.h"
#include "miniaudio.h"

std::string programName = "wextract";

float windowWidth = 800;
float windowHeight = 600;

double regionStart = 1.0f;
double regionEnd = 2.0f;
bool mouseClicked = false;
bool dragging = false;

constexpr ma_uint32 blockSize { 256 };

static void framebuffer_size_callback(GLFWwindow *window, int width, int height)
{
    // glViewport(0, 0, width, height);
    windowWidth = static_cast<float>(width);
    windowHeight = static_cast<float>(height);
}

static void data_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount)
{
    // In playback mode copy data to pOutput. In capture mode read data from pInput. In full-duplex mode, both
    // pOutput and pInput will be valid and you can move data from pInput into pOutput. Never process more than
    // frameCount frames.
    static std::array<std::array<float, blockSize>, 2> buffers;
    static ma_uint32 framesSinceNoteOn = 0;

    float* audioBuffer[2] { buffers[0].data(), buffers[1].data() };
    float* output = reinterpret_cast<float*>(pOutput);
    sfz::Sfizz* synth = reinterpret_cast<sfz::Sfizz*>(pDevice->pUserData);
    ma_uint32 renderIdx { 0 };
    framesSinceNoteOn += frameCount;
    while (frameCount > 0) {
        ma_uint32 frames = std::min(frameCount, blockSize);
        synth->renderBlock(audioBuffer, frames);
        for (ma_uint32 i = 0; i < frames; i++) {
            output[renderIdx + 2 * i] = buffers[0][i];
            output[renderIdx + 2 * i + 1] = buffers[1][i];
        }
        renderIdx += 2 * frames;
        frameCount -= frames;
    }

    if (framesSinceNoteOn > pDevice->sampleRate) {
        synth->noteOn(0, 60, 127);
        framesSinceNoteOn = 0;
    }
}

int main(int argc, char *argv[])
{
    sfz::Sfizz synth;
    synth.setSamplesPerBlock(blockSize);
    synth.loadSfzString("", "<region> sample=*sine loop_mode=one_shot ampeg_attack=0.03 ampeg_release=1");
    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format   = ma_format_f32;   
    config.playback.channels = 0;               // Set to 0 to use the device's native channel count.
    config.sampleRate        = 0;               // Set to 0 to use the device's native sample rate.
    config.dataCallback      = data_callback;
    config.pUserData         = &synth;

    ma_device device;
    defer { ma_device_uninit(&device); };

    if (ma_device_init(NULL, &config, &device) != MA_SUCCESS) {
        std::cerr << "[ERROR] Failed to initialize device\n";
        return -1;
    }
    synth.setSampleRate(device.sampleRate);
    ma_device_start(&device);
    
    std::cout << "Backend: " << ma_get_backend_name(device.pContext->backend) << '\n';
    std::cout << "Sample rate: " << device.sampleRate << '\n';
    if (!glfwInit()) {
        std::cerr << "[ERROR] Couldn't initialize GLFW\n";
        return -1;
    }
    std::cout << "[INFO] GLFW initialized\n";

    // setup GLFW window

    glfwWindowHint(GLFW_DOUBLEBUFFER , 1);
    glfwWindowHint(GLFW_DEPTH_BITS, 24);
    glfwWindowHint(GLFW_STENCIL_BITS, 8);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    std::string glsl_version = "";
#ifdef __APPLE__
    // GL 4.3 + GLSL 150
    glsl_version = "#version 150";
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE); // required on Mac OS
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
#elif __linux__
    // GL 4.3 + GLSL 150
    glsl_version = "#version 150";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
#elif _WIN32
    // GL 4.3 + GLSL 130
    glsl_version = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
#endif

    GLFWwindow *window = glfwCreateWindow(windowWidth, windowHeight, 
        programName.c_str(), NULL, NULL);
    defer { 
        glfwDestroyWindow(window);
        glfwTerminate();
    };

    if (!window) {
        std::cerr << "[ERROR] Couldn't create a GLFW window\n";
        return -1;
    }

    // watch window resizing
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwMakeContextCurrent(window);
    // VSync
    glfwSwapInterval(1);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cerr << "[ERROR] Couldn't initialize GLAD" << '\n';
        return -1;
    }

    std::cout << "[INFO] GLAD initialized\n";
    std::cout << "[INFO] OpenGL from glad "
              << GLVersion.major << "." << GLVersion.minor
              << '\n';

    int actualWindowWidth, actualWindowHeight;
    glfwGetWindowSize(window, &actualWindowWidth, &actualWindowHeight);
    glViewport(0, 0, actualWindowWidth, actualWindowHeight);
    glClearColor(0.12, 0.12, 0.12, 1.0f);

    ma_decoder decoder;
    ma_decoder_config decoder_config = ma_decoder_config_init(ma_format_f32, 2, device.sampleRate);
    auto result = ma_decoder_init_file("sine_c3.wav", &decoder_config, &decoder);
    if (result != MA_SUCCESS)
        return -1;

    defer { ma_decoder_uninit(&decoder); };

    auto numFrames = ma_decoder_get_length_in_pcm_frames(&decoder);
    auto numChannels = decoder.internalChannels;
    std::cout << "Number of frames: " << numFrames << '\n';
    std::cout << "Number of channels: " << numChannels << '\n';

    std::vector<float> file;
    file.resize(numFrames * numChannels);
    std::fill(file.begin(), file.end(), 0.0f);
    if (numFrames != ma_decoder_read_pcm_frames(&decoder, file.data(), numFrames))
        std::cout << "Error reading the file!\n";

    std::vector<ImPlotPoint> plot;
    plot.resize(numFrames);
    float period = 1 / static_cast<float>(decoder.outputSampleRate);
    for (int i = 0; i < numFrames; i++) {
        plot[i].x = i * period;
        plot[i].y = file[2 * i + 1];
    }

    // --- rendering loop
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    defer { ImGui::DestroyContext(); };
    ImPlot::CreateContext();
    defer { ImPlot::DestroyContext(); };

    ImGuiIO& io = ImGui::GetIO(); (void)io;

    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version.c_str());

    while (!glfwWindowShouldClose(window))
    {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(windowWidth, windowHeight));
        ImGui::Begin("Main", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove);
        if (ImPlot::BeginPlot("Soundfile")) {
            ImPlot::PlotLine("", &plot[0].x, &plot[0].y, numFrames, 0, sizeof(ImPlotPoint));
            ImPlot::DragLineX("DragStart", &regionStart);
            ImPlot::DragLineX("DragStop", &regionEnd);
            ImPlot::GetPlotDrawList()->AddRectFilled(
                ImPlot::PlotToPixels(ImPlotPoint(regionStart, ImPlot::GetPlotLimits().Y.Min)),
                ImPlot::PlotToPixels(ImPlotPoint(regionEnd, ImPlot::GetPlotLimits().Y.Max)),
                ImGui::GetColorU32(ImVec4(1, 1, 1, 0.25f))
            );
            // ImPlot::SetNextLineStyle(ImVec4(1, 1, 1, 1), 2.0f);
            // ImPlot::PlotVLines("VLine", &linePosition, 1, 0);
            // if (dragging && !ImGui::IsMouseClicked(0))
            //     dragging = false;

            // if (ImPlot::IsPlotHovered() && ImGui::IsMouseClicked(0) && !mouseClicked) {
            //     auto context = ImPlot::GetCurrentContext();
            //     if (!dragging) {
            //         auto point = ImPlot::GetPlotMousePos();
            //         const float grab_size = 5.0f;
            //         float yt = context->CurrentPlot->PlotRect.Min.y;
            //         float yb = context->CurrentPlot->PlotRect.Max.y;
            //         float x  = std::lround(ImPlot::PlotToPixels(linePosition, 0).x);
            //         const bool outside = x < (context->CurrentPlot->PlotRect.Min.x - grab_size / 2) || x > (context->CurrentPlot->PlotRect.Max.x + grab_size / 2);
            //         if (!outside)
            //             dragging = true;
            //     }

            //     mouseClicked = true;
            // }

            ImPlot::EndPlot();
        }
        ImGui::End();

        // rendering
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
        glfwWaitEvents();
    }

    return 0;
}