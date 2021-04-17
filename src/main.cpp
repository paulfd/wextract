#define NOMINMAX
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <string>
#include <array>
#include <algorithm>
#include <iostream>
#include <atomic>
#include <mutex>
#include <fmt/core.h>
#include "ghc/fs_std.hpp"
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

std::mutex callbackLock;
std::atomic_flag playFile;
std::atomic_flag playFileOff;
std::atomic_flag playWave;
std::atomic_flag playWaveOff;

double regionStart = 1.0f;
double regionEnd = 2.0f;
double sustainLevel = 0.0f;
struct NamedPlotPoint
{
    NamedPlotPoint(double x, double y, std::string name)
    : x(x), y(y), name(std::move(name)) {}
    double x, y;
    std::string name;
};
std::vector<NamedPlotPoint> points;
unsigned pointCounter { 0 };

constexpr ma_uint32 blockSize { 256 };

static const std::string filename = "sine_c3.wav";
static const auto sfzPath = fs::current_path() / "base.sfz";
static const std::string baseSample = "<region> loop_mode=one_shot key=60 ";
static const std::string baseOutput = "<region> key=61 sample=*sine";
static std::string eg = "";

static void sortPoints()
{
    std::sort(points.begin(), points.end(), [] (NamedPlotPoint& lhs, NamedPlotPoint& rhs) {
        return lhs.x < rhs.x;
    });
}

static void reloadSfzFile(sfz::Sfizz& synth)
{
    std::string sfz = fmt::format("<region> loop_mode=one_shot key=60 sample={}"
        "<region> key=61 sample=*sine ", filename);
    defer { 
        std::lock_guard<std::mutex> lock { callbackLock };
        synth.loadSfzString(sfzPath.string(), sfz);
    };

    if (points.size() < 2)        
        return;

    bool nonzeroEnd = points.back().y > 0.0f;
    eg = "eg01_ampeg=1 ";
    if (nonzeroEnd)
        eg += fmt::format("eg01_sustain={}\n", points.size());
    else
        eg += "loop_mode=one_shot\n";

    auto start = points[0].x;
    eg += fmt::format("eg01_time1=0 eg01_level1={:.2f}", points[0].y / sustainLevel);
    for (unsigned i = 1, n = points.size(); i < n; ++i) {
        eg += fmt::format("\neg01_time{0}={1:.2f} eg01_level{0}={2:.2f}",
            i + 1, points[i].x - start, points[i].y / sustainLevel);
    }
    if (nonzeroEnd) {
        eg += fmt::format("\neg01_time{0}={1:.2f} eg01_level{0}={2:.2f}",
                points.size() + 1, points.back().x - start + 1.0f, 0.0f);
    }
    
    sfz += eg;
}

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

    std::unique_lock<std::mutex> lock { callbackLock, std::try_to_lock };
    if (!lock.owns_lock())
        return;

    float* audioBuffer[2] { buffers[0].data(), buffers[1].data() };
    float* output = reinterpret_cast<float*>(pOutput);
    sfz::Sfizz* synth = reinterpret_cast<sfz::Sfizz*>(pDevice->pUserData);
    ma_uint32 renderIdx { 0 };
    framesSinceNoteOn += frameCount;
    
    if (!playFile.test_and_set())
        synth->noteOn(0, 60, 127);

    if (!playFileOff.test_and_set())
        synth->noteOff(1, 60, 127);

    if (!playWave.test_and_set())
        synth->noteOn(0, 61, 127);

    if (!playWaveOff.test_and_set())
        synth->noteOff(1, 61, 127);

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
}

int main(int argc, char *argv[])
{
    sfz::Sfizz synth;
    synth.setSamplesPerBlock(blockSize);
    // synth.loadSfzString("", "<region> sample=*sine loop_mode=one_shot ampeg_attack=0.03 ampeg_release=1");
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
    auto result = ma_decoder_init_file(filename.c_str(), &decoder_config, &decoder);
    if (result != MA_SUCCESS){
        std::cout << "Could not open sound file\n";
        return -1;
    }
    defer { ma_decoder_uninit(&decoder); };
    reloadSfzFile(synth);

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
        sustainLevel = std::max(plot[i].y, sustainLevel);
    }

    // --- rendering loop
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    defer { ImGui::DestroyContext(); };
    ImPlot::CreateContext();
    defer { ImPlot::DestroyContext(); };

    ImGuiIO& io = ImGui::GetIO();

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
        ImGui::BeginChild("Plot", ImVec2(windowWidth - 150.0f, 300.0f));
        if (ImPlot::BeginPlot("Soundfile")) {
            ImPlot::PlotLine("", &plot[0].x, &plot[0].y, numFrames, 0, sizeof(ImPlotPoint));
            ImPlot::DragLineX("DragStart", &regionStart);
            ImPlot::DragLineX("DragStop", &regionEnd);
            ImPlot::DragLineY("SustainLevel", &sustainLevel, true, ImGui::GetStyleColorVec4(ImGuiCol_NavHighlight));
            ImPlot::GetPlotDrawList()->AddRectFilled(
                ImPlot::PlotToPixels(ImPlotPoint(regionStart, ImPlot::GetPlotLimits().Y.Min)),
                ImPlot::PlotToPixels(ImPlotPoint(regionEnd, ImPlot::GetPlotLimits().Y.Max)),
                ImGui::GetColorU32(ImVec4(1, 1, 1, 0.25f))
            );
            
            auto mousePlotPos = ImPlot::GetPlotMousePos();
            auto mousePos = ImGui::GetMousePos();
            if (ImPlot::IsPlotHovered() && ImGui::IsMouseClicked(0) && io.KeyCtrl) {
                points.emplace_back(mousePlotPos.x, mousePlotPos.y, std::to_string(pointCounter++));
                sortPoints();
            }

            if (ImGui::IsMouseReleased(0))
                reloadSfzFile(synth);

            const ImVec4 color = ImGui::GetStyleColorVec4(ImGuiCol_Text);
            const ImU32 col32 = ImGui::ColorConvertFloat4ToU32(color);
            auto it = points.begin();
            while (it != points.end()) {
                NamedPlotPoint& p = *it;
                ImPlot::DragPoint(p.name.c_str(), &p.x, &p.y, false);
                p.y = std::max(0.0, p.y);
                
                if (ImGui::IsItemHovered() || ImGui::IsItemActive()) { 
                    if (ImGui::IsMouseDoubleClicked(0)) {
                        it = points.erase(it);
                        reloadSfzFile(synth);
                        continue;
                    }

                    if (ImGui::IsMouseDragging(0)) 
                        sortPoints();

                    const ImVec2 pos = ImPlot::PlotToPixels(p.x, p.y);
                    ImPlotContext& gp = *ImPlot::GetCurrentContext();
                    gp.CurrentPlot->PlotHovered = false;
                    ImVec2 label_pos = pos + 
                        ImVec2(16 * GImGui->Style.MouseCursorScale, 8 * GImGui->Style.MouseCursorScale);
                    char buff1[32];
                    char buff2[32];
                    ImPlot::LabelAxisValue(gp.CurrentPlot->XAxis, gp.XTicks, p.x, buff1, 32);
                    ImPlot::LabelAxisValue(gp.CurrentPlot->YAxis[0], gp.YTicks[0], p.y, buff2, 32);
                    gp.Annotations.Append(label_pos, ImVec2(0.0001f,0.00001f), col32, ImPlot::CalcTextColor(color), 
                        true, "%s,%s", buff1, buff2);
                }
                ++it;
            }

            if (points.size() > 1) {
                for (unsigned i = 0, end = points.size() - 1; i < end; ++i) {
                    ImPlot::GetPlotDrawList()->AddLine(
                        ImPlot::PlotToPixels(points[i].x, points[i].y),
                        ImPlot::PlotToPixels(points[i + 1].x, points[i + 1].y),
                        col32
                    );
                }
            }

            ImPlot::EndPlot();
        }
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("Buttons", ImVec2(150.0f, 300.0f));

        ImGui::Button("Play");
        if (ImGui::IsItemActive() && ImGui::IsMouseClicked(0))
            playFile.clear();
        
        if (ImGui::IsItemHovered() && ImGui::IsMouseReleased(0))
            playFileOff.clear();

        ImGui::Button("Play Wave");
        if (ImGui::IsItemActive() && ImGui::IsMouseClicked(0))
            playWave.clear();
        
        if (ImGui::IsItemHovered() && ImGui::IsMouseReleased(0))
            playWaveOff.clear();

        ImGui::EndChild();

        ImGui::InputTextMultiline("##source", const_cast<char*>(eg.c_str()), eg.size(), 
            ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 12), ImGuiInputTextFlags_ReadOnly);
        ImGui::End();

        // rendering
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
        glfwWaitEvents();
    }

    return 0;
}