// SPDX-License-Identifier: MIT
// Copyright (c) 2021 Paul Ferrand

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
    Synth() = delete;
    Synth(int blockSize);
    void callback(float* output, int frameCount);
    void loadString(std::string_view sfz);

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