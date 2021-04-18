#define NOMINMAX
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <string>
#include <array>
#include <algorithm>
#include <numeric>
#include <iostream>
#include <atomic>
#include <mutex>
#include <thread>
#include <complex>
#include <tuple>
#include <fmt/core.h>
#include <Eigen/Dense>
#include "ghc/fs_std.hpp"
#include "imgui.h"
#include "implot.h"
#include "implot_internal.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "sfizz.hpp"
#include "defer.h"
#include "miniaudio.h"
using namespace std::complex_literals;

std::string programName = "wextract";

template<class T>
constexpr T pi = T { 3.14159265358979323846 };
constexpr int tableSize { 1024 };

float windowWidth = 800;
float windowHeight = 600;

std::mutex callbackLock;

std::atomic_flag playFile;
std::atomic_flag playFileOff;
std::atomic_flag playWave;
std::atomic_flag playWaveOff;
std::atomic_flag closeComputationModal;
std::atomic_flag updateWavetable;
std::atomic_flag reloadSfz;

double regionStart = 0.65f;
double regionEnd = 1.0f;
double sustainLevel = 0.5f;

using HarmonicVector = std::vector<std::pair<float, std::complex<float>>>;
HarmonicVector harmonics;
std::vector<float> wavetable;

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

static const std::string filename = "trompette8_c2.wav";
static std::string tableFilename = "";
static const auto sfzPath = fs::current_path() / "base.sfz";
static const std::string baseSample = "<region> loop_mode=one_shot key=60 ";
static const std::string baseOutput = "<region> key=36 sample=*sine";
static std::string eg = "";
static std::string sfzFile = "";

static void sortPoints()
{
    std::sort(points.begin(), points.end(), [] (NamedPlotPoint& lhs, NamedPlotPoint& rhs) {
        return lhs.x < rhs.x;
    });
}

static void reloadSfzFile(sfz::Sfizz& synth)
{
    sfzFile = fmt::format("<region> loop_mode=one_shot key=60 sample={}\n"
        "<region> key=36 ", filename);
    defer { 
        std::lock_guard<std::mutex> lock { callbackLock };
        synth.loadSfzString(sfzPath.string(), sfzFile);
    };

    if (!tableFilename.empty())
        sfzFile += fmt::format("oscillator=on sample={}\n", tableFilename);
    else
        sfzFile += "sample=*sine\n";

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
    for (size_t i = 1, n = points.size(); i < n; ++i) {
        eg += fmt::format("\neg01_time{0}={1:.2f} eg01_level{0}={2:.2f}",
            i + 1, points[i].x - start, points[i].y / sustainLevel);
    }
    if (nonzeroEnd) {
        eg += fmt::format("\neg01_time{0}={1:.2f} eg01_level{0}={2:.2f} eg01_shape{0}=-8",
                points.size() + 1, points.back().x - start + 0.01f, 0.0f);
    }
    
    sfzFile += eg;
}

static std::pair<float, std::complex<float>> frequencyPeakSearch(float* signal, size_t size, float coarseFrequency, 
    float sampleRate, float centsRange = 50, int pointsPerCents=100)
{
    using namespace Eigen;
    float logFreq = std::log2(coarseFrequency);
    VectorXf freq = VectorXf::LinSpaced(2 * pointsPerCents, 
        logFreq - centsRange / 1200, logFreq + centsRange / 1200);
    freq = pow(2.0f, freq.array());
    VectorXf time = VectorXf::LinSpaced(size, 0, static_cast<float>(size - 1)) / sampleRate;

    MatrixXcf projectionMatrix = 
        exp(2.0if * pi<float> * (freq * time.transpose()).array());
    VectorXcf projected = projectionMatrix * Map<VectorXf>(signal, size);

    unsigned maxIdx = 0;
    float maxHarmonic = 0.0f;

    for (unsigned i = 0; i < projected.size(); ++i) {
        float harmonic = std::abs(projected[i]);
        if (harmonic > maxHarmonic) {
            maxIdx = i;
            maxHarmonic = harmonic;
        }
    }

    return std::make_pair(freq[maxIdx], projected[maxIdx]);
}

static std::vector<float> buildWavetable(const HarmonicVector& harmonics, int size,
    bool normalizePower = true)
{
    using namespace Eigen;
    
    std::vector<double> table;
    std::vector<float> output;
    table.resize(size);
    output.reserve(size);
    std::fill(table.begin(), table.end(), 0.0);

    if (harmonics.empty()) {
        fmt::print("Empty harmonics\n");
        std::fill_n(std::back_inserter(output), size, 0.0f);
        return output;
    }

    using RowArrayXd = Array<double, 1, Dynamic>;
    ArrayXd time = ArrayXd::LinSpaced(size, 0, static_cast<double>(size - 1));
    time /= static_cast<double>(size);
    Map<ArrayXd> mappedTable { table.data(), size };

    for (const auto& [f, h] : harmonics) {
        double freqIndex = std::round(f / harmonics.front().first);
        double phase = std::arg(h);
        double magnitude = std::abs(h);
        // fmt::print("Harmonic at {:.2f} ({}) Hz: {:.3f} exp (i pi {:.3f})\n", f, freqIndex, magnitude, phase);
        mappedTable += magnitude * (2.0 * pi<double> * freqIndex * time + phase).sin();
    }

    if (normalizePower) {
        double squaredNorm = std::accumulate(harmonics.begin(), harmonics.end(), 0.0, 
            [] (double lhs, const auto& rhs) { return lhs + std::pow(std::abs(rhs.second), 2); });
        double norm = std::sqrt(squaredNorm);
        mappedTable /= norm;
    }

    size_t zeroIndex = 0;
    double zeroValue = mappedTable.maxCoeff();
    for (int i = 0; i < size; ++i) {
        double absValue = std::abs(mappedTable[i]);
        if (absValue < zeroValue) {
            zeroIndex = i;
            zeroValue = absValue;
        }
    }
    ArrayXd head = mappedTable.head(zeroIndex);
    ArrayXd tail = mappedTable.tail(size - zeroIndex);
    mappedTable << tail, head;

    std::transform(table.begin(), table.end(), std::back_inserter(output),
            [](double x) { return static_cast<float>(x); });
    return output;
}

static std::vector<float> extractSignalRange(const float* source, double regionStart, double regionEnd, 
    double samplePeriod, int stride = 2, int offset = 0)
{
    std::vector<float> signal;

    if (regionStart > regionEnd)
        std::swap(regionStart, regionEnd);

    int rangeStart = static_cast<int>(regionStart / samplePeriod);
    int rangeEnd = static_cast<int>(regionEnd / samplePeriod);
    int rangeSize = rangeEnd - rangeStart;
    if (rangeSize == 0)
        return signal;
        
    signal.resize(rangeSize);
    for (int t = 0, s = rangeStart; s < rangeEnd; ++t, ++s)
        signal[t] = source[stride * s + offset];
    
    return signal;
}

static void framebuffer_size_callback(GLFWwindow *window, int width, int height)
{
    // glViewport(0, 0, width, height);
    windowWidth = static_cast<float>(width);
    windowHeight = static_cast<float>(height);
}

static void data_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount)
{
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
        synth->noteOn(0, 36, 127);

    if (!playWaveOff.test_and_set())
        synth->noteOff(1, 36, 127);

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
    playFile.test_and_set();
    playFileOff.test_and_set();
    playWave.test_and_set();
    playWaveOff.test_and_set();
    closeComputationModal.test_and_set();
    reloadSfz.test_and_set();
    updateWavetable.test_and_set();

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
    synth.setSampleRate(static_cast<float>(device.sampleRate));
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
    glfwGetWindowSize(window, &actualWindowWidth, &actualWindowHeight);
    glViewport(0, 0, actualWindowWidth, actualWindowHeight);
    glClearColor(0.12f, 0.12f, 0.12f, 1.0f);

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
    double period = 1 / static_cast<double>(decoder.outputSampleRate);
    for (int i = 0; i < numFrames; i++) {
        plot[i].x = i * period;
        plot[i].y = file[2 * i];
        sustainLevel = std::max(std::abs(plot[i].y), sustainLevel);
    }
    std::vector<ImPlotPoint> tablePlot;
    double tablePeriod = 1.0 / static_cast<double>(tableSize);
    tablePlot.resize(tableSize);
    for (int i = 0; i < tableSize; i++) {
        tablePlot[i].x = i * tablePeriod;
        tablePlot[i].y = 0.0;
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

        if (!reloadSfz.test_and_set())
            reloadSfzFile(synth);

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(windowWidth, windowHeight));
        ImGui::Begin("Main", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
        
        if (ImGui::BeginTabBar("Tabs")) {
            if (ImGui::BeginTabItem("Sound file")) {
                if (ImGui::BeginChild("Plot", ImVec2(windowWidth - 150.0f, 300.0f))) {
                    ImPlot::SetNextPlotLimitsY(-sustainLevel + 0.1f, sustainLevel + 0.1f);
                    if (ImPlot::BeginPlot("Soundfile", "time (seconds)", nullptr,
                            ImVec2(-1, 0), 0, 0, ImPlotAxisFlags_Lock)) {
                        ImPlot::PlotLine("", &plot[0].x, &plot[0].y, static_cast<int>(numFrames), 0, sizeof(ImPlotPoint));
                        ImPlot::DragLineX("DragStart", &regionStart);
                        ImPlot::DragLineX("DragStop", &regionEnd);
                        ImPlot::DragLineY("SustainLevel", &sustainLevel, true, 
                            ImGui::GetStyleColorVec4(ImGuiCol_NavHighlight));
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
                    ImGui::EndChild();
                }
                
                ImGui::SameLine();
                if (ImGui::BeginChild("Buttons", ImVec2(150.0f, 300.0f))) {
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

                    if (ImGui::Button("Find frequency")) {
                        std::thread( [period, file] {

                            auto signal = extractSignalRange(file.data(), regionStart, regionEnd, period);
                            float sampleRate = 1.0f / static_cast<float>(period);
                            auto [frequency, harmonic] = 
                                frequencyPeakSearch(signal.data(), signal.size(), 65.0f, sampleRate);

                            fmt::print("Frequency: {:.2f} Hz (Harmonic: {} + i{})\n", 
                                frequency, harmonic.real(), harmonic.imag());
                        }).detach();
                    }

                    if (ImGui::Button("Find harmonics")) {
                        ImGui::OpenPopup("Computation");
                        std::thread( [period, file] {
                            harmonics.clear();
                            auto signal = extractSignalRange(file.data(), regionStart, regionEnd, period);
                            float sampleRate = 1.0f / static_cast<float>(period);
                            float rootFrequency = 65.0f;
                            float frequencyLimit = std::min(sampleRate / 2.0f, rootFrequency * 16);

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
                            closeComputationModal.clear();
                            updateWavetable.clear();
                            ma_encoder encoder;
                            ma_encoder_config config = 
                                ma_encoder_config_init(ma_resource_format_wav, ma_format_f32, 1, 44100);
                            tableFilename = "table.wav";
                            ma_result result = ma_encoder_init_file(tableFilename.c_str(), &config, &encoder);
                            defer {
                                ma_encoder_uninit(&encoder);
                            };
                            ma_encoder_write_pcm_frames(&encoder, wavetable.data(), tableSize);
                            if (result != MA_SUCCESS) {
                                std::cerr << "Error writing down table file";
                                tableFilename = "";
                            }

                            reloadSfz.clear();                
                        }).detach();
                    }
                    
                    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
                    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
                    if (ImGui::BeginPopupModal("Computation", nullptr, 
                            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize)) {
                        ImGui::Text("Computing wavetables... (%d harmonics)", harmonics.size());
                        if (!closeComputationModal.test_and_set())
                            ImGui::CloseCurrentPopup();

                        ImGui::EndPopup();
                    }
                    ImGui::EndChild();
                }
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Wavetable and SFZ")) {
                auto tabSize = ImGui::TabItemCalcSize("", false);
                auto windowHeight = ImGui::GetWindowHeight() - tabSize.y - 20;
                auto plotWidth = windowWidth / 2.5f;
                ImPlot::SetNextPlotLimitsX(0.0, 1.0);
                if (ImPlot::BeginPlot("Wavetable", "Period", nullptr,
                    ImVec2(plotWidth, std::min(windowHeight, plotWidth)), 0, ImPlotAxisFlags_Lock,
                    ImPlotAxisFlags_AutoFit | ImPlotAxisFlags_NoTickLabels)) {
                    if (!updateWavetable.test_and_set()) {
                        for (int i = 0, n = static_cast<int>(wavetable.size()); i < tableSize && i < n; i++)
                            tablePlot[i].y = wavetable[i];
                    }
                    ImPlot::PlotLine("", &tablePlot[0].x, &tablePlot[0].y, tableSize, 0, sizeof(ImPlotPoint));
                    ImPlot::EndPlot();
                }

                ImGui::SameLine();
                ImGui::InputTextMultiline("##source", const_cast<char*>(sfzFile.c_str()), sfzFile.size(), 
                    ImVec2(-1, windowHeight), ImGuiInputTextFlags_ReadOnly);
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        
        
        ImGui::End();


        // rendering
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    return 0;
}