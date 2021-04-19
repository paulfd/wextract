#include <atomic>
#include <mutex>
#include <vector>
#include <array>
#include <filesystem>
#include <string_view>
#include "sfizz.hpp"

namespace fs = std::filesystem;

class Synth
{
public:
    Synth(int blockSize)
    : blockSize_(blockSize) 
    {
        buffers_[0].resize(blockSize);
        buffers_[1].resize(blockSize);
        synth_.setSamplesPerBlock(blockSize);
        playSample_.test_and_set();
        playSampleOff_.test_and_set();
        playWave_.test_and_set();
        playWaveOff_.test_and_set();
    }

    void callback(float* output, int frameCount)
    {
        std::unique_lock<std::mutex> lock { callbackLock_, std::try_to_lock };
        if (!lock.owns_lock())
            return;

        float* audioBuffer[2] { buffers_[0].data(), buffers_[1].data() };
        int renderIdx { 0 };
        
        if (!playSample_.test_and_set())
            synth_.noteOn(0, sampleNote_, 127);

        if (!playSampleOff_.test_and_set())
            synth_.noteOff(1, sampleNote_, 127);

        if (!playWave_.test_and_set())
            synth_.noteOn(0, waveNote_, 127);

        if (!playWaveOff_.test_and_set())
            synth_.noteOff(1, waveNote_, 127);

        while (frameCount > 0) {
            int frames = std::min(frameCount, blockSize_);
            synth_.renderBlock(audioBuffer, frames);
            for (int i = 0; i < frames; i++) {
                output[renderIdx + 2 * i] = buffers_[0][i];
                output[renderIdx + 2 * i + 1] = buffers_[1][i];
            }
            renderIdx += 2 * frames;
            frameCount -= frames;
        }
    }

    void loadString(std::string_view sfz) {
        std::lock_guard<std::mutex> lock { callbackLock_ };
        synth_.loadSfzString(sfzPath_.string(), std::string(sfz));
    }

    void setSampleRate(unsigned sampleRate) { synth_.setSampleRate(static_cast<float>(sampleRate)); }
    void sampleOn() { playSample_.clear(); };
    void sampleOff() { playSampleOff_.clear(); };
    void waveOn() { playWave_.clear(); };
    void waveOff() { playWaveOff_.clear(); };
    void setWaveNote(int noteNumber) { waveNote_ = noteNumber; }
    void setSampleNote(int noteNumber) { sampleNote_ = noteNumber; }
    void setSamplePath(const fs::path& path)
    {
        sfzPath_ = path.parent_path() / "base.sfz";            
    }

    fs::path getRootDirectory() const { return sfzPath_.parent_path(); }

private:
    sfz::Sfizz synth_;
    std::array<std::vector<float>, 2> buffers_;
    int blockSize_;
    int waveNote_ { 36 };
    int sampleNote_ { 127 };
    fs::path sfzPath_ { fs::current_path() / "base.sfz" };
    std::mutex callbackLock_;
    std::atomic_flag playSample_;
    std::atomic_flag playSampleOff_;
    std::atomic_flag playWave_;
    std::atomic_flag playWaveOff_;
};