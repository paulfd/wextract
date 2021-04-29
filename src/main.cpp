// SPDX-License-Identifier: MIT
// Copyright (c) 2021 Paul Ferrand

#define NOMINMAX
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <string>
#include <string_view>
#include <array>
#include <filesystem>
#include <algorithm>
#include <iostream>
#include <atomic>
#include <mutex>
#include <thread>
#include <fmt/core.h>
#include "imgui.h"
#include "implot.h"
#include "implot_internal.h"
#include "imgui_impl_glfw.h"
#include "imfilebrowser.h"
#include "imgui_impl_opengl3.h"
#include "sfizz.hpp"
#include "synth.h"
#include "helpers.h"
#include "threadpool.h"
#include "defer.h"
#include "miniaudio.h"
#include "kiss_fft.h"

namespace fs = std::filesystem;

std::string programName = "wextract";

constexpr int blockSize { 256 };
ThreadPool pool { 1 };

float windowWidth { 1000 };
float windowHeight { 620 };
constexpr float buttonGroupSize { 220.0f };
constexpr float groupHeight { 300.0f };

Synth synth { blockSize };

std::string filename = "";
std::vector<float> file;
int numFrames { 0 };
int numChannels { 0 };
unsigned sampleRate { 44100 };
double samplePeriod { 1.0 / static_cast<double>(sampleRate) };

std::atomic_flag closeComputationModal;
std::atomic_flag updateWavetable;
std::atomic_flag reloadSfz;

double regionStart = 0.65f;
double regionEnd = 1.0f;
double sustainLevel = 0.5f;
int waveNote { 36 };
float reverb { 0.0f };
float volume { 0.0f };

int rootNote { 36 };
int numHarmonics { 16 };
int tableSize { 1024 };
HarmonicVector harmonics;
std::vector<float> wavetable;

int offset { 0 };
double yMax { 0.5 };
std::atomic_flag resetAxis;
std::vector<ImPlotPoint> plot;
std::vector<ImPlotPoint> tablePlot;
std::vector<ImPlotPoint> frequencyPlot {{ 0.0, 0.0 }};
std::vector<ImPlotPoint> frequencyTablePlot {{ 0.0, 0.0 }};

std::string tableFilename = "";
std::string sfzFile = "";

fs::path lastDirectory { fs::current_path() };
ImGui::FileBrowser openDialog;
ImGui::FileBrowser saveDialog { ImGuiFileBrowserFlags_EnterNewFilename };


struct NamedPlotPoint
{
    NamedPlotPoint(double x, double y, std::string name)
    : x(x), y(y), name(std::move(name)) {}
    double x, y;
    std::string name;
};

std::vector<NamedPlotPoint> points;
unsigned pointCounter { 0 };

static void sortPoints()
{
    std::sort(points.begin(), points.end(), [] (NamedPlotPoint& lhs, NamedPlotPoint& rhs) {
        return lhs.x < rhs.x;
    });
}

static void rebuildSfzFile()
{
    sfzFile.clear();
    if (reverb > 0.0f) {
        sfzFile += fmt::format("<effect> bus=main type=fverb reverb_size=50 reverb_type=large_hall\n");
        sfzFile += fmt::format("    reverb_dry=100 reverb_wet={:.1f} reverb_input=100\n", reverb);
    }

    if (!filename.empty())
        sfzFile += fmt::format("<region> loop_mode=one_shot key=127 volume={:.1f} sample={}\n", volume, filename);
    
    sfzFile += fmt::format("<region> key={} ", waveNote);

    if (!tableFilename.empty())
        sfzFile += fmt::format("oscillator=on sample={}\n", tableFilename);
    else
        sfzFile += "sample=*sine\n";

    if (points.size() < 2)        
        return;

    bool nonzeroEnd = points.back().y > 0.0f;
    sfzFile += "eg01_ampeg=1 ";
    if (nonzeroEnd)
        sfzFile += fmt::format("eg01_sustain={}\n", points.size());
    else
        sfzFile += "loop_mode=one_shot\n";

    auto start = points[0].x;
    sfzFile += fmt::format("eg01_time1=0 eg01_level1={:.2f}", points[0].y / sustainLevel);
    for (size_t i = 1, n = points.size(); i < n; ++i) {
        sfzFile += fmt::format("\neg01_time{0}={1:.2f} eg01_level{0}={2:.2f}",
            i + 1, points[i].x - points[i - 1].x, points[i].y / sustainLevel);
    }
    if (nonzeroEnd) {
        sfzFile += fmt::format("\neg01_time{0}={1:.2f} eg01_level{0}={2:.2f} eg01_shape{0}=-3",
                points.size() + 1, 0.1f, 0.0f);
    }
}

static void drawPlot()
{
    ImGuiIO& io = ImGui::GetIO();
    ImPlot::SetNextPlotLimitsY(-yMax - 0.1f, yMax + 0.1f, ImGuiCond_Always);
    if (!resetAxis.test_and_set()) {
        float xMax = static_cast<float>(numFrames * samplePeriod);
        ImPlot::SetNextPlotLimitsX(0.0f, xMax, ImGuiCond_Always);
    }

    if (ImPlot::BeginPlot(filename.c_str(), "time (seconds)", nullptr,
            ImVec2(-1, 0), ImPlotFlags_AntiAliased)) {
        ImPlot::PlotLine("", &plot[0].x, &plot[0].y, 
            static_cast<int>(plot.size()), 0, sizeof(ImPlotPoint));
        ImPlot::DragLineX("DragStart", &regionStart);
        ImPlot::DragLineX("DragStop", &regionEnd);
        ImPlot::DragLineY("SustainLevel", &sustainLevel, true, 
            ImGui::GetStyleColorVec4(ImGuiCol_NavHighlight));
        sustainLevel = std::max(0.0, sustainLevel);
        if (ImGui::IsItemHovered() && ImGui::IsMouseReleased(0)) 
            reloadSfz.clear();

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
                    reloadSfz.clear();
                    continue;
                }

                if (ImGui::IsMouseDragging(0))
                    sortPoints();

                if (ImGui::IsMouseReleased(0)) 
                    reloadSfz.clear();

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
            for (size_t i = 0, end = points.size() - 1; i < end; ++i) {
                ImPlot::GetPlotDrawList()->AddLine(
                    ImPlot::PlotToPixels(points[i].x, points[i].y),
                    ImPlot::PlotToPixels(points[i + 1].x, points[i + 1].y),
                    col32
                );
            }
        }
        ImPlot::EndPlot();
    }
}

static void readFileSample(std::string_view path)
{
    ma_decoder decoder;
    ma_decoder_config decoder_config = ma_decoder_config_init(ma_format_f32, 2, sampleRate);
    auto result = ma_decoder_init_file(path.data(), &decoder_config, &decoder);
    if (result != MA_SUCCESS){
        std::cout << "Could not open sound file\n";
        return;
    }
    defer { ma_decoder_uninit(&decoder); };

    numFrames = static_cast<int>(ma_decoder_get_length_in_pcm_frames(&decoder));
    numChannels = decoder.internalChannels;
    std::cout << "Number of frames: " << numFrames << '\n';
    std::cout << "Number of channels: " << numChannels << '\n';

    // Read the base file
    file.resize(numFrames * numChannels);
    std::fill(file.begin(), file.end(), 0.0f);
    if (numFrames != ma_decoder_read_pcm_frames(&decoder, file.data(), numFrames))
        std::cout << "Error reading the file!\n";
}

static void updateFilePlot()
{
    plot.clear();

    if (numFrames == 0) {
        plot.push_back({0, 0});
        return;
    }

    plot.resize(numFrames);
    yMax = 0.0;
    for (int i = 0; i < numFrames; i++) {
        plot[i].x = i * samplePeriod;
        plot[i].y = file[2 * i + offset];
        yMax = std::max(std::abs(plot[i].y), yMax);
    }
    regionStart = samplePeriod * numFrames / 2.0;
    regionEnd = regionStart * 1.2;
    sustainLevel = yMax * 0.9;
    resetAxis.clear();
}

static void updateTablePlot()
{
    tablePlot.resize(tableSize);
    double tablePeriod = 1.0 / static_cast<double>(tableSize);
    for (int i = 0; i < tableSize; i++) {
        tablePlot[i].x = i * tablePeriod;
        tablePlot[i].y = wavetable[i];
    }
}

static void drawWaveAndFile()
{
    auto plotWidth = 300.0f;
    const int size = static_cast<int>(tablePlot.size());
    ImPlot::SetNextPlotLimitsX(0.0, 1.0);
    if (ImPlot::BeginPlot("Wavetable", nullptr, nullptr,
        ImVec2(plotWidth, plotWidth), 0, 
        ImPlotAxisFlags_Lock | ImPlotAxisFlags_NoTickLabels,
        ImPlotAxisFlags_AutoFit | ImPlotAxisFlags_NoTickLabels)) {
        ImPlot::PlotLine("", &tablePlot[0].x, &tablePlot[0].y, size, 0, sizeof(ImPlotPoint));
        ImPlot::EndPlot();
    }

    ImGui::SameLine();
    ImGui::InputTextMultiline("##source", const_cast<char*>(sfzFile.c_str()), sfzFile.size(), 
        ImVec2(-1, plotWidth), ImGuiInputTextFlags_ReadOnly);
}

static bool saveWavetable(const fs::path& path, const float* wavetable, int size)
{
    ma_encoder encoder;
    ma_encoder_config config = 
        ma_encoder_config_init(ma_resource_format_wav, ma_format_f32, 1, 44100);
        
    std::string tablePath = path.string();
    ma_result result = ma_encoder_init_file(tablePath.c_str(), &config, &encoder);
    defer { ma_encoder_uninit(&encoder); };
    if (result != MA_SUCCESS) {
        std::cerr << "Error writing down table file" << "\n";
        return false;
    }

    auto written = ma_encoder_write_pcm_frames(&encoder, wavetable, size);
    if (written != size) {
        std::cerr << "Could not write all frames" << "\n";

        if (fs::exists(path))
            fs::remove(path);
        
        return false;
    }

    return true;
}

static void drawButtons()
{
    const auto padding = ImGui::GetStyle().WindowPadding;
    const auto buttonWidth = ImGui::GetWindowSize().x;
    if (ImGui::Button("Open file", ImVec2(buttonWidth, 0.0f))) {
        openDialog.SetPwd(lastDirectory);
        openDialog.Open();
    }

    if (ImGui::RadioButton("Use left", &offset, 0))
        updateFilePlot();

    ImGui::SameLine();
    if (ImGui::RadioButton("Use right", &offset, 1))
        updateFilePlot();
    
    ImGui::SliderFloat("Volume", &volume, -60.0f, 40.0f);
    if (ImGui::IsItemHovered() && ImGui::IsMouseReleased(0))
        reloadSfz.clear();

    ImGui::SliderFloat("Reverb", &reverb, 0.0f, 100.0f);
    if (ImGui::IsItemHovered() && ImGui::IsMouseReleased(0))
        reloadSfz.clear();

    ImGui::Button("Play sample", ImVec2(buttonWidth, 0.0f));
    if (ImGui::IsItemActive() && ImGui::IsMouseClicked(0))
        synth.sampleOn();

    if (ImGui::IsItemHovered() && ImGui::IsMouseReleased(0))
        synth.sampleOff();

    ImGui::Separator();
    ImGui::SliderInt("Root note", &rootNote, 0, 128);
    ImGui::SliderInt("Harmonics", &numHarmonics, 0, 128);
    ImGui::InputInt("Table size", &tableSize);
    if (ImGui::Button("Extract table", ImVec2(buttonWidth, 0.0f))) {
        ImGui::OpenPopup("Computation");
        pool.enqueue( [] {
            harmonics.clear();
            auto signal = extractSignalRange(file.data(), regionStart, regionEnd, 
                samplePeriod, numChannels, offset);
            float rootFrequency = 440.0f * std::pow(2.0, (rootNote - 49) / 12.0);
            float frequencyLimit = std::min(sampleRate / 2.0f, rootFrequency * numHarmonics);
            float searchFrequency = 0.0f;
            while (searchFrequency < frequencyLimit) {
                searchFrequency += rootFrequency;
                auto [frequency, harmonic] = 
                    frequencyPeakSearch(signal.data(), signal.size(), searchFrequency, sampleRate);

                if (harmonics.empty() 
                    || std::abs(harmonics.back().first - frequency) > rootFrequency)
                    harmonics.emplace_back(frequency, harmonic);
            }

            wavetable = buildWavetable(harmonics, tableSize);
            updateTablePlot();
            closeComputationModal.clear();

            tableFilename = "table.wav";
            if (!saveWavetable(synth.getRootDirectory() / tableFilename, 
                wavetable.data(), wavetable.size())) {
                tableFilename = "";
            }
            reloadSfz.clear();                
        });
    }


    ImGui::Separator();
    if (ImGui::SliderInt("Note", &waveNote, 0, 126))
        reloadSfz.clear();

    ImGui::Button("Play table", ImVec2(buttonWidth, 0.0f));
    if (ImGui::IsItemActive() && ImGui::IsMouseClicked(0)) {
        synth.setWaveNote(waveNote);
        synth.waveOn();
    }
    
    if (ImGui::IsItemHovered() && ImGui::IsMouseReleased(0))
        synth.waveOff();

    if (ImGui::Button("Save table", ImVec2(buttonWidth, 0.0f))) {
        saveDialog.SetPwd(lastDirectory);
        saveDialog.Open();
    }

    if (ImGui::Button("Frequency response", ImVec2(buttonWidth, 0.0f))) {
        ImGui::OpenPopup("Frequency");

        // Clear the frequency plots
        frequencyPlot.clear();
        frequencyTablePlot.clear();
        frequencyPlot.push_back({0, 0});
        frequencyTablePlot.push_back({0, 0});

        pool.enqueue( [] {
            auto signal = extractSignalRange(file.data(), regionStart, regionEnd, 
                samplePeriod, numChannels, offset);
            size_t fftSize = static_cast<size_t>(
                kiss_fft_next_fast_size(static_cast<int>(signal.size()))
            );
            kiss_fft_cfg cfg = kiss_fft_alloc(fftSize, false, 0, 0);
            defer { kiss_fft_free(cfg); };
            std::vector<kiss_fft_cpx> input (fftSize);
            std::vector<kiss_fft_cpx> output (fftSize);
            for (size_t i = 0; i < signal.size(); ++i) {
                input[i].r = signal[i];
                input[i].i = 0.0f;
            }

            for (size_t i = signal.size(); i < fftSize; ++i) {
                input[i].r = 0.0f;
                input[i].i = 0.0f;
            }

            kiss_fft(cfg, input.data(), output.data());
            
            frequencyPlot.resize(fftSize / 2);
            double frequencyStep = sampleRate / fftSize;
            for (size_t i = 0, n = fftSize / 2; i < n; ++i) {
                frequencyPlot[i].x = i * frequencyStep;
                frequencyPlot[i].y = 10.0 * std::log10(
                    output[i].r * output[i].r + output[i].i * output[i].i
                );   
            }

            frequencyTablePlot.resize(fftSize / 2);
            float waveFrequency = 440.0f * std::pow(2.0, (waveNote - 49) / 12.0);
            float phaseIncrement = waveFrequency / static_cast<float>(sampleRate);
            float phase = 0.0f;
            size_t tableSentinel = (fftSize / tableSize) * tableSize;
            for (size_t i = 0; i < tableSentinel; ++i) {
                float position = phase * tableSize;
                size_t index = static_cast<size_t>(position);
                float interp = phase - index;
                input[i].r = (1.0f - interp) * wavetable[index]
                    + interp * wavetable[(index + 1) % tableSize];
                fmt::print("{} {} {} {} \n", phase, index, interp, input[i].r);
                phase += phaseIncrement;
                phase -= static_cast<int>(phase);
                phase += phase < 0.0f;
            }
            for (size_t i = tableSentinel; i < fftSize; ++i)
                input[i].r = 0.0f;

            kiss_fft(cfg, input.data(), output.data());
            for (size_t i = 0, n = fftSize / 2; i < n; ++i) {
                float samplePeriod = 1 / static_cast<float>(sampleRate);
                frequencyTablePlot[i].x = i * samplePeriod;
                frequencyTablePlot[i].y = input[i].r;
            }
            // for (size_t i = 0, n = fftSize / 2; i < n; ++i) {
            //     frequencyTablePlot[i].x = i * frequencyStep;
            //     frequencyTablePlot[i].y = 10.0 * std::log10(
            //         output[i].r * output[i].r + output[i].i * output[i].i
            //     );
            // }
        });
    }


    // Modal popup
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Computation", nullptr, 
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize)) {
        ImGui::Text("Computing wavetables... (%d harmonics)", harmonics.size());
        if (!closeComputationModal.test_and_set())
            ImGui::CloseCurrentPopup();

        ImGui::EndPopup();
    }

    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopup("Frequency", 
        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize)) {
        if (ImPlot::BeginPlot("Frequency response", "Frequency (Hz)", nullptr,
            ImVec2(600, 0), ImPlotFlags_AntiAliased, ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit)) {
            // ImPlot::PlotLine("Sample", &frequencyPlot[0].x, &frequencyPlot[0].y, 
            //     static_cast<int>(frequencyPlot.size()), 0, sizeof(ImPlotPoint));
            ImPlot::PlotLine("Table", &frequencyTablePlot[0].x, &frequencyTablePlot[0].y, 
                static_cast<int>(frequencyTablePlot.size()), 0, sizeof(ImPlotPoint));
            ImPlot::EndPlot();
        }
        ImGui::EndPopup();
    }
}

static void framebuffer_size_callback(GLFWwindow *window, int width, int height)
{
    // glViewport(0, 0, width, height);
    windowWidth = static_cast<float>(width);
    windowHeight = static_cast<float>(height);
}

static void data_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount)
{
    float* output = reinterpret_cast<float*>(pOutput);
    Synth* synth = reinterpret_cast<Synth*>(pDevice->pUserData);
    synth->callback(output, static_cast<int>(frameCount));
}

int main(int argc, char *argv[])
{
    closeComputationModal.test_and_set();
    reloadSfz.test_and_set();
    updateWavetable.test_and_set();

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
    sampleRate = device.sampleRate;
    samplePeriod = 1 / static_cast<double>(sampleRate);
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

    GLFWwindow *window = glfwCreateWindow(
        static_cast<int>(windowWidth), static_cast<int>(windowHeight), 
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
    fmt::print("{}\n", actualWindowHeight);
    glfwGetWindowSize(window, &actualWindowWidth, &actualWindowHeight);
    glViewport(0, 0, actualWindowWidth, actualWindowHeight);
    glClearColor(0.12f, 0.12f, 0.12f, 1.0f);
    
    // Set browser properties
    openDialog.SetTitle("Open file");
    openDialog.SetTypeFilters({ ".wav" });
    
    saveDialog.SetTitle("Save table");
    
    wavetable.resize(tableSize);
    for (int i = 0; i < tableSize; ++i)
        wavetable[i] = std::sin( 2 * 3.1415926535f * i / static_cast<float>(tableSize) );

    updateFilePlot();
    updateTablePlot();

    rebuildSfzFile();
    synth.loadString(sfzFile);

    // --- rendering loop
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    defer { ImGui::DestroyContext(); };
    ImPlot::CreateContext();
    defer { ImPlot::DestroyContext(); };

    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version.c_str());

    while (!glfwWindowShouldClose(window))
    {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        if (!reloadSfz.test_and_set()) {
            rebuildSfzFile();
            synth.loadString(sfzFile);
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(windowWidth, windowHeight));
        ImGui::Begin("Main", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);

        const auto padding = ImGui::GetStyle().WindowPadding;
        const auto plotWidth = windowWidth  - 3 * padding.x - buttonGroupSize;
        if (ImGui::BeginChild("Plot", ImVec2(plotWidth, groupHeight))) {
            drawPlot();
            ImGui::EndChild();
        }
                
        ImGui::SameLine();
        if (ImGui::BeginChild("Buttons", ImVec2(buttonGroupSize, groupHeight))) {
            drawButtons();
            ImGui::EndChild();
        }

        drawWaveAndFile();

        openDialog.Display();
        if(openDialog.HasSelected())
        {
            auto selected = openDialog.GetSelected();
            lastDirectory = selected.parent_path();
            synth.setSamplePath(selected);
            filename = selected.filename().string();
            tableFilename = "";
            harmonics.clear();
            reloadSfz.clear();

            updateTablePlot();
            pool.enqueue([selected] {
                readFileSample(selected.string());
                updateFilePlot();
            });
            openDialog.ClearSelected();
        }

        saveDialog.Display();
        if(saveDialog.HasSelected())
        {
            auto selected = saveDialog.GetSelected();
            lastDirectory = selected.parent_path();
            pool.enqueue([selected] { 
                saveWavetable(selected, wavetable.data(), wavetable.size());
            });
            saveDialog.ClearSelected();
        }

        ImGui::End();

        // rendering
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    if (!tableFilename.empty()) {
        auto tablePath = synth.getRootDirectory() / tableFilename;
        if (fs::exists(tablePath))
            fs::remove(tablePath);
    }

    return 0;
}